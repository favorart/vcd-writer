// Micro-benchmark of VCDWriter hot paths.
// Usage: vcd_bench [output_dir]   (default: current directory)
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "vcd_writer.h"

using namespace vcd;
using Clock = std::chrono::steady_clock;

namespace {

std::string bits(unsigned value, unsigned width)
{
    std::string s(width, '0');
    for (unsigned i = 0; i < width; ++i)
        if (value & (1u << i))
            s[width - 1 - i] = '1';
    return s;
}

// best of `runs` wall time in milliseconds
double measure(const char *name, size_t ops, int runs, const std::function<void()> &fn)
{
    double best = 1e300;
    for (int r = 0; r < runs; ++r)
    {
        const auto t0 = Clock::now();
        fn();
        const auto t1 = Clock::now();
        best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::printf("%-28s %10.1f ms  %8.1f ns/op\n", name, best, best * 1e6 / double(ops));
    return best;
}

constexpr unsigned VARS = 1000;  // per kind
constexpr unsigned STEPS = 1000;
constexpr unsigned WIDTH = 32;

} // namespace

int main(int argc, char **argv)
{
    const std::string dir = (argc > 1) ? std::string(argv[1]) + "/" : std::string();
    const std::string file = dir + "bench.vcd";
    const int runs = 3;

    // precomputed values: the benchmark measures the writer, not the value formatting
    std::vector<std::string> vec_values(STEPS);
    for (unsigned t = 0; t < STEPS; ++t)
        vec_values[t] = bits(t * 2654435761u, WIDTH);
    const std::string scalars[2] = { "0", "1" };

    auto register_all = [&](VCDWriter &w, std::vector<VarPtr> &vec, std::vector<VarPtr> &sc) {
        for (unsigned i = 0; i < VARS; ++i)
        {
            const std::string scope = "top.m" + std::to_string(i % 50);
            vec.push_back(w.register_var(scope, "v" + std::to_string(i), VariableType::wire, WIDTH));
            sc.push_back(w.register_var(scope, "s" + std::to_string(i), VariableType::wire, 1));
        }
    };

    // every var changes on every step
    measure("change(VarPtr) all changed", size_t(2) * VARS * STEPS, runs, [&] {
        HeadPtr head = makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, "bench");
        VCDWriter w(file, head);
        std::vector<VarPtr> vec, sc;
        register_all(w, vec, sc);
        for (unsigned t = 0; t < STEPS; ++t)
            for (unsigned i = 0; i < VARS; ++i)
            {
                w.change(vec[i], t, vec_values[(t + i) % STEPS]);
                w.change(sc[i], t, scalars[(t + i) & 1]);
            }
    });

    // values do not change: only validation + deduplication, nothing is written
    measure("change(VarPtr) unchanged", size_t(2) * VARS * STEPS, runs, [&] {
        HeadPtr head = makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, "bench");
        VCDWriter w(file, head);
        std::vector<VarPtr> vec, sc;
        register_all(w, vec, sc);
        for (unsigned t = 0; t < STEPS; ++t)
            for (unsigned i = 0; i < VARS; ++i)
            {
                w.change(vec[i], t, vec_values[i]);
                w.change(sc[i], t, scalars[i & 1]);
            }
    });

    // short values are left-extended
    measure("change(VarPtr) short values", size_t(VARS) * STEPS, runs, [&] {
        HeadPtr head = makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, "bench");
        VCDWriter w(file, head);
        std::vector<VarPtr> vec, sc;
        register_all(w, vec, sc);
        for (unsigned t = 0; t < STEPS; ++t)
            for (unsigned i = 0; i < VARS; ++i)
                w.change(vec[i], t, (t + i) & 1 ? "101" : "11");
    });

    // lookup by scope and name on every change
    std::vector<std::string> scopes(VARS), names(VARS);
    for (unsigned i = 0; i < VARS; ++i)
    {
        scopes[i] = "top.m" + std::to_string(i % 50);
        names[i] = "v" + std::to_string(i);
    }
    measure("change(scope, name)", size_t(VARS) * STEPS, runs, [&] {
        HeadPtr head = makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, "bench");
        VCDWriter w(file, head);
        std::vector<VarPtr> vec, sc;
        register_all(w, vec, sc);
        for (unsigned t = 0; t < STEPS; ++t)
            for (unsigned i = 0; i < VARS; ++i)
                w.change(scopes[i], names[i], t, vec_values[(t + i) % STEPS]);
    });

    // registration of many vars + header
    constexpr unsigned REG = 100000;
    measure("register_var + header", REG, runs, [&] {
        HeadPtr head = makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, "bench");
        VCDWriter w(file, head);
        for (unsigned i = 0; i < REG; ++i)
            w.register_var("top.a" + std::to_string(i % 100) + ".b" + std::to_string(i % 7),
                           "v" + std::to_string(i), VariableType::wire, 8);
        w.flush();
    });

    std::remove(file.c_str());
    return 0;
}
