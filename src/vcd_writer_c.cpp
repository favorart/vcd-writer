#include "vcd_writer_c.h"

#include <ios>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "vcd_writer.h"

// the C enumerations mirror the C++ ones
static_assert(int(vcd_scope_count_) == int(vcd::ScopeType::_count_));
static_assert(int(vcd_scope_module) == int(vcd::ScopeType::module));
static_assert(int(vcd_var_count_) == int(vcd::VariableType::_count_));
static_assert(int(vcd_var_event) == int(vcd::VariableType::event));
static_assert(int(vcd_var_wor) == int(vcd::VariableType::wor));
static_assert(int(vcd_unit_count_) == int(vcd::TimeScaleUnit::_count_));
static_assert(int(vcd_unit_ns) == int(vcd::TimeScaleUnit::ns));
static_assert(int(vcd_timescale_hundred) == int(vcd::TimeScale::hundred));

struct vcd_var
{
    vcd::VarPtr var;
};

struct vcd_writer
{
    vcd::Writer writer;
    std::vector<std::unique_ptr<vcd_var>> vars;  // handles given to the user
};

namespace {
thread_local std::string last_error;

// Run *fn*, turn its exceptions into a status and keep the message for vcd_last_error()
template <class Fn>
vcd_status call(Fn &&fn) noexcept
{
    try
    {
        fn();
        last_error.clear();
        return vcd_ok;
    }
    catch (const vcd::PhaseException &e) { last_error = e.what(); return vcd_error_phase; }
    catch (const vcd::TypeException &e)  { last_error = e.what(); return vcd_error_type; }
    catch (const vcd::Exception &e)      { last_error = e.what(); return vcd_error_io; }
    catch (const std::ios_base::failure &e) { last_error = e.what(); return vcd_error_io; }
    catch (const std::bad_alloc &e)      { last_error = e.what(); return vcd_error_memory; }
    catch (const std::exception &e)      { last_error = e.what(); return vcd_error_unknown; }
    catch (...)                          { last_error = "unknown error"; return vcd_error_unknown; }
}

vcd_status null_argument()
{
    last_error = "VCD error: NULL argument";
    return vcd_error_argument;
}

const char* or_empty(const char *s) { return s ? s : ""; }
}

// -----------------------------
vcd_status vcd_open(vcd_writer **writer, const char *filename, vcd_timescale quan, vcd_timescale_unit unit,
                    const char *date, const char *comment, const char *version, uint64_t init_timestamp)
{
    if (!writer || !filename)
        return null_argument();
    *writer = nullptr;
    return call([&] {
        auto header = vcd::make_header(vcd::TimeScale(quan), vcd::TimeScaleUnit(unit),
                                       date ? std::string(date) : vcd::utils::now(), or_empty(comment), or_empty(version));
        *writer = new vcd_writer{ vcd::Writer(filename, std::move(header), init_timestamp), {} };
    });
}

// -----------------------------
vcd_status vcd_close(vcd_writer *writer, const uint64_t *timestamp)
{
    if (!writer)
        return vcd_ok;
    const std::unique_ptr<vcd_writer> owner(writer);
    return call([&] { writer->writer.close(timestamp); });
}

// -----------------------------
vcd_status vcd_register_var(vcd_writer *writer, const char *scope, const char *name, vcd_var_type type,
                            unsigned size, const char *init, vcd_var **var)
{
    if (!writer || !scope || !name || !var)
        return null_argument();
    *var = nullptr;
    return call([&] {
        auto handle = std::make_unique<vcd_var>();
        handle->var = writer->writer.register_var(scope, name, vcd::VariableType(type), size,
                                                  init ? init : vcd::VarValue{ vcd::Bit::undef });
        writer->vars.push_back(std::move(handle));
        *var = writer->vars.back().get();
    });
}

// -----------------------------
vcd_status vcd_change(vcd_writer *writer, const vcd_var *var, uint64_t timestamp, const char *value, int *changed)
{
    if (!writer || !var || !value)
        return null_argument();
    return call([&] {
        const bool written = writer->writer.change(var->var, timestamp, value);
        if (changed)
            *changed = written;
    });
}

// -----------------------------
vcd_status vcd_change_by_name(vcd_writer *writer, const char *scope, const char *name, uint64_t timestamp,
                              const char *value, int *changed)
{
    if (!writer || !scope || !name || !value)
        return null_argument();
    return call([&] {
        const bool written = writer->writer.change(scope, name, timestamp, value);
        if (changed)
            *changed = written;
    });
}

// -----------------------------
vcd_status vcd_dump_off(vcd_writer *writer, uint64_t timestamp)
{
    if (!writer)
        return null_argument();
    return call([&] { writer->writer.dump_off(timestamp); });
}

// -----------------------------
vcd_status vcd_dump_on(vcd_writer *writer, uint64_t timestamp)
{
    if (!writer)
        return null_argument();
    return call([&] { writer->writer.dump_on(timestamp); });
}

// -----------------------------
vcd_status vcd_flush(vcd_writer *writer, const uint64_t *timestamp)
{
    if (!writer)
        return null_argument();
    return call([&] { writer->writer.flush(timestamp); });
}

// -----------------------------
vcd_status vcd_set_scope_type(vcd_writer *writer, const char *scope, vcd_scope_type type)
{
    if (!writer || !scope)
        return null_argument();
    return call([&] { writer->writer.set_scope_type(scope, vcd::ScopeType(type)); });
}

// -----------------------------
vcd_status vcd_set_scope_default_type(vcd_writer *writer, vcd_scope_type type)
{
    if (!writer)
        return null_argument();
    return call([&] { writer->writer.set_scope_default_type(vcd::ScopeType(type)); });
}

// -----------------------------
vcd_status vcd_set_scope_sep(vcd_writer *writer, const char *scope_sep)
{
    if (!writer || !scope_sep)
        return null_argument();
    return call([&] { writer->writer.set_scope_sep(scope_sep); });
}

// -----------------------------
const char* vcd_last_error(void)
{
    return last_error.c_str();
}
