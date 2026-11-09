#include "vcd_writer_c.h"

#include <exception>
#include <memory>
#include <string>

#include "vcd_writer.h"

// the C enumerations mirror the C++ ones
static_assert(int(vcd_scope_count_) == int(vcd::ScopeType::_count_));
static_assert(int(vcd_var_count_) == int(vcd::VariableType::_count_));
static_assert(int(vcd_unit_count_) == int(vcd::TimeScaleUnit::_count_));
static_assert(int(vcd_timescale_hundred) == int(vcd::TimeScale::hundred));

namespace {
thread_local std::string last_error;

// Run *fn*, turn its exception into a status and keep the message for vcd_last_error()
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
    catch (const std::exception &e)      { last_error = e.what(); return vcd_error; }
}

// The handles are the C++ objects themselves
vcd::Writer& writer_of(vcd_writer *writer) { return *reinterpret_cast<vcd::Writer*>(writer); }
// A non-owning pointer: no reference counting on the hot path, the writer owns the variable
vcd::VarPtr var_of(vcd_var *var) { return { vcd::VarPtr{}, reinterpret_cast<vcd::Variable*>(var) }; }

// The value in a reused buffer: no memory allocation per change in a steady state
const vcd::VarValue& value_of(const char *value)
{
    thread_local vcd::VarValue buf;
    return buf.assign(value);
}

void set_changed(int *changed, bool written)
{
    if (changed)
        *changed = written;
}

const char* or_empty(const char *s) { return s ? s : ""; }
}

// -----------------------------
vcd_status vcd_open(vcd_writer **writer, const char *filename, vcd_timescale quan, vcd_timescale_unit unit,
                    const char *date, const char *comment, const char *version, uint64_t init_timestamp)
{
    *writer = nullptr;
    return call([&] {
        auto header = vcd::make_header(vcd::TimeScale(quan), vcd::TimeScaleUnit(unit), date ? date : vcd::utils::now(),
                                       or_empty(comment), or_empty(version));
        *writer = reinterpret_cast<vcd_writer*>(new vcd::Writer(filename, std::move(header), init_timestamp));
    });
}

// -----------------------------
vcd_status vcd_close(vcd_writer *writer, const uint64_t *timestamp)
{
    const std::unique_ptr<vcd::Writer> owner(reinterpret_cast<vcd::Writer*>(writer));
    return owner ? call([&] { owner->close(timestamp); }) : vcd_ok;
}

// -----------------------------
vcd_status vcd_register_var(vcd_writer *writer, const char *scope, const char *name, vcd_var_type type,
                            unsigned size, const char *init, vcd_var **var)
{
    *var = nullptr;
    return call([&] {
        const auto p = writer_of(writer).register_var(scope, name, vcd::VariableType(type), size,
                                                      init ? init : vcd::VarValue{ vcd::Bit::undef });
        *var = reinterpret_cast<vcd_var*>(p.get());
    });
}

// -----------------------------
vcd_status vcd_change(vcd_writer *writer, vcd_var *var, uint64_t timestamp, const char *value, int *changed)
{
    return call([&] { set_changed(changed, writer_of(writer).change(var_of(var), timestamp, value_of(value))); });
}

// -----------------------------
vcd_status vcd_change_by_name(vcd_writer *writer, const char *scope, const char *name, uint64_t timestamp,
                              const char *value, int *changed)
{
    return call([&] { set_changed(changed, writer_of(writer).change(scope, name, timestamp, value_of(value))); });
}

// -----------------------------
vcd_status vcd_dump_off(vcd_writer *writer, uint64_t timestamp)
{
    return call([&] { writer_of(writer).dump_off(timestamp); });
}

// -----------------------------
vcd_status vcd_dump_on(vcd_writer *writer, uint64_t timestamp)
{
    return call([&] { writer_of(writer).dump_on(timestamp); });
}

// -----------------------------
vcd_status vcd_flush(vcd_writer *writer, const uint64_t *timestamp)
{
    return call([&] { writer_of(writer).flush(timestamp); });
}

// -----------------------------
vcd_status vcd_set_scope_type(vcd_writer *writer, const char *scope, vcd_scope_type type)
{
    return call([&] { writer_of(writer).set_scope_type(scope, vcd::ScopeType(type)); });
}

// -----------------------------
vcd_status vcd_set_scope_default_type(vcd_writer *writer, vcd_scope_type type)
{
    return call([&] { writer_of(writer).set_scope_default_type(vcd::ScopeType(type)); });
}

// -----------------------------
vcd_status vcd_set_scope_sep(vcd_writer *writer, const char *scope_sep)
{
    return call([&] { writer_of(writer).set_scope_sep(scope_sep); });
}

// -----------------------------
const char* vcd_last_error(void)
{
    return last_error.c_str();
}
