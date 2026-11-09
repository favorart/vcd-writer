// Python bindings of vcd-writer: the `vcdwriter` module, a thin nanobind wrapper of the C++ API.
// Values are VCD strings, as in C++: "1", "x", "1010" (vector bits), "3.14" (real), "text" (string).
#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>

#include <filesystem>
#include <new>
#include <optional>

#include "vcd_variable.h"
#include "vcd_writer.h"

namespace nb = nanobind;
using namespace nb::literals;
using namespace vcd;

namespace {
// The pointer argument of Writer::flush() / Writer::close(): null if *ts* is None
const TimeStamp* ptr(const std::optional<TimeStamp> &ts) { return ts ? &*ts : nullptr; }
}

NB_MODULE(vcdwriter, m)
{
    m.doc() = "Writer of Value Change Dump (VCD) files, IEEE 1364-2005, 18.2 (C++ vcd-writer).";

    auto error = nb::exception<Exception>(m, "VCDError");
    nb::exception<PhaseException>(m, "VCDPhaseError", error);
    nb::exception<TypeException>(m, "VCDTypeError", error);

    nb::enum_<ScopeType>(m, "ScopeType")
        .value("begin", ScopeType::begin).value("fork", ScopeType::fork).value("function", ScopeType::function)
        .value("module", ScopeType::module).value("task", ScopeType::task);
    nb::enum_<VariableType>(m, "VariableType")
        .value("wire", VariableType::wire).value("reg", VariableType::reg).value("string", VariableType::string)
        .value("parameter", VariableType::parameter).value("integer", VariableType::integer)
        .value("real", VariableType::real).value("realtime", VariableType::realtime).value("time", VariableType::time)
        .value("event", VariableType::event).value("supply0", VariableType::supply0)
        .value("supply1", VariableType::supply1).value("tri", VariableType::tri).value("triand", VariableType::triand)
        .value("trior", VariableType::trior).value("trireg", VariableType::trireg).value("tri0", VariableType::tri0)
        .value("tri1", VariableType::tri1).value("wand", VariableType::wand).value("wor", VariableType::wor);
    nb::enum_<TimeScale>(m, "TimeScale")
        .value("one", TimeScale::one).value("ten", TimeScale::ten).value("hundred", TimeScale::hundred);
    nb::enum_<TimeScaleUnit>(m, "TimeScaleUnit")
        .value("s", TimeScaleUnit::s).value("ms", TimeScaleUnit::ms).value("us", TimeScaleUnit::us)
        .value("ns", TimeScaleUnit::ns).value("ps", TimeScaleUnit::ps).value("fs", TimeScaleUnit::fs);

    nb::class_<Variable>(m, "Variable")
        .def_ro("name", &Variable::name)
        .def_ro("type", &Variable::type)
        .def_ro("size", &Variable::size)
        .def_ro("ident", &Variable::ident);

    nb::class_<Writer>(m, "Writer")
        .def("__init__",
             [](Writer *self, const std::filesystem::path &file, TimeScale quan, TimeScaleUnit unit,
                const std::optional<std::string> &date, const std::string &comment, const std::string &version,
                TimeStamp init_timestamp) {
                 new (self) Writer(file.string(), make_header(quan, unit, date ? *date : utils::now(), comment, version),
                                   init_timestamp);
             },
             "file"_a, "timescale_quan"_a = TimeScale::one, "timescale_unit"_a = TimeScaleUnit::ns,
             "date"_a = nb::none(), "comment"_a = "", "version"_a = "", "init_timestamp"_a = 0)
        .def("register_var", &Writer::register_var,
             "scope"_a, "name"_a, "type"_a = VariableType::integer, "size"_a = 0, "init"_a = std::string(1, Bit::undef))
        .def("change", [](Writer &w, Variable &v, TimeStamp t, const char *s) {
                 thread_local std::string buf; buf.assign(s); return w.change(v.shared_from_this(), t, buf); },
             "var"_a, "timestamp"_a, "value"_a)
        .def("change", nb::overload_cast<const std::string&, const std::string&, TimeStamp, const VarValue&>(&Writer::change),
             "scope"_a, "name"_a, "timestamp"_a, "value"_a)
        .def("var", &Writer::var, "scope"_a, "name"_a)
        .def("dump_off", &Writer::dump_off, "timestamp"_a)
        .def("dump_on", &Writer::dump_on, "timestamp"_a)
        .def("flush", [](Writer &w, std::optional<TimeStamp> ts) { w.flush(ptr(ts)); }, "timestamp"_a = nb::none())
        .def("close", [](Writer &w, std::optional<TimeStamp> ts) { w.close(ptr(ts)); }, "timestamp"_a = nb::none())
        .def("set_scope_type", &Writer::set_scope_type, "scope"_a, "type"_a)
        .def("set_scope_default_type", &Writer::set_scope_default_type, "type"_a)
        .def("set_scope_sep", &Writer::set_scope_sep, "scope_sep"_a)
        .def("__enter__", [](Writer &w) -> Writer& { return w; }, nb::rv_policy::reference)
        .def("__exit__", [](Writer &w, nb::args) { w.close(); });
}
