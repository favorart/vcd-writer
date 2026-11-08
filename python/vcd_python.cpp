// Python bindings of vcd-writer: the `vcdwriter` module, CPython C API only (no third-party dependency).
// The API follows pyvcd (vcd.writer.VCDWriter) for the features both libraries have.
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <climits>
#include <initializer_list>
#include <ios>
#include <memory>
#include <new>
#include <string>
#include <string_view>

#include "vcd_variable.h"
#include "vcd_writer.h"

namespace vcd {
namespace {
// Python names of the enumerations, in the order of their values
constexpr std::array<const char*, size_t(VariableType::_count_)> VAR_TYPES{
    "wire", "reg", "string", "parameter", "integer", "real", "realtime", "time", "event",
    "supply0", "supply1", "tri", "triand", "trior", "trireg", "tri0", "tri1", "wand", "wor" };
constexpr std::array<const char*, size_t(ScopeType::_count_)> SCOPE_TYPES{ "begin", "fork", "function", "module", "task" };
constexpr std::array<const char*, size_t(TimeScaleUnit::_count_)> TIMESCALE_UNITS{ "s", "ms", "us", "ns", "ps", "fs" };

constexpr unsigned INT_BITS = 64;        // bits of the fast path of an integer value
constexpr size_t DOUBLE_TEXT_SIZE = 32;  // the shortest round-trip text of a double fits

// Objects of the module (single-phase initialization)
PyObject *vcd_error = nullptr;    // VCDError
PyObject *phase_error = nullptr;  // VCDPhaseError(VCDError)
PyObject *type_error = nullptr;   // VCDTypeError(VCDError, ValueError)
PyTypeObject *variable_type = nullptr;

// -----------------------------
// A Python error is set already
struct PyError {};

struct Decref
{
    void operator()(PyObject *p) const { Py_XDECREF(p); }
};
using Ref = std::unique_ptr<PyObject, Decref>;

// *p* (a new reference) or PyError if it is null
PyObject* check(PyObject *p);

PyObject* none() { Py_RETURN_NONE; }
PyObject* boolean(bool b) { return PyBool_FromLong(b); }

// Run *f*: the C++ exceptions become Python ones
template <class F>
PyObject* guarded(F &&f) noexcept;

// UTF-8 text of the str *s*
std::string_view text(PyObject *s, const char *what);
// Value of the enumeration *E* named by the str *name* in *names*
template <class E, size_t N>
E enum_value(const std::array<const char*, N> &names, PyObject *name, const char *what);
// Timestamp of the int *ts*
TimeStamp timestamp(PyObject *ts);
// Header with the timescale "<number> <unit>" or "<number><unit>", e.g. "1 ns", "10us"
HeadPtr make_py_header(std::string_view timescale, PyObject *date, const char *comment, const char *version);

// VCD value of the Python *value* of *var*: str as is, bool, int, float, None ('z')
const VarValue& to_value(const Variable &var, PyObject *value, std::string &buf);
// Bits of *value* (its least significant *bits*), the leading zeros are dropped
void append_bits(std::string &out, unsigned long long value, unsigned bits);
// Python int of a vector: two's complement in *size* bits
void int_to_bits(const Variable &var, PyObject *value, std::string &out);

// -----------------------------
// vcdwriter.Variable: handle of a registered variable
struct VariableObject
{
    PyObject_HEAD
    VarPtr var;  // constructed in place by make_variable()
};

PyObject* make_variable(VarPtr var);
void variable_dealloc(PyObject *self);
PyObject* variable_new(PyTypeObject*, PyObject*, PyObject*);
PyObject* variable_repr(PyObject *self);
const Variable& variable_of(PyObject *self) { return *reinterpret_cast<VariableObject*>(self)->var; }

// -----------------------------
// vcdwriter.Writer
struct WriterState
{
    Writer writer;
    std::string buf;  // converted value: no memory allocation per change in a steady state
};

struct WriterObject
{
    PyObject_HEAD
    WriterState *state;  // null until __init__()
};

int writer_init(PyObject *self, PyObject *args, PyObject *kwargs);
void writer_dealloc(PyObject *self);
// The state of an initialized writer
WriterState& state_of(PyObject *self);

PyObject* writer_register_var(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject* writer_change(PyObject *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames);
PyObject* writer_var(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject* writer_dump_off(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject* writer_dump_on(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject* writer_flush(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject* writer_close(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject* writer_set_scope_type(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject* writer_set_scope_default_type(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject* writer_set_scope_sep(PyObject *self, PyObject *args, PyObject *kwargs);
PyObject* writer_enter(PyObject *self, PyObject*);
PyObject* writer_exit(PyObject *self, PyObject*);

// -----------------------------
PyObject* check(PyObject *p)
{
    if (p == nullptr)
        throw PyError{};
    return p;
}

// -----------------------------
template <class F>
PyObject* guarded(F &&f) noexcept
{
    try { return f(); }
    catch (const PyError&) {}
    catch (const PhaseException &e) { PyErr_SetString(phase_error, e.what()); }
    catch (const TypeException &e) { PyErr_SetString(type_error, e.what()); }
    catch (const Exception &e) { PyErr_SetString(vcd_error, e.what()); }
    catch (const std::bad_alloc&) { PyErr_NoMemory(); }
    catch (const std::ios_base::failure &e) { PyErr_SetString(PyExc_OSError, e.what()); }
    catch (const std::exception &e) { PyErr_SetString(PyExc_RuntimeError, e.what()); }
    return nullptr;
}

// -----------------------------
std::string_view text(PyObject *s, const char *what)
{
    if (!PyUnicode_Check(s))
        throw TypeException{ std::string("Expected a str ") + what };
    Py_ssize_t size = 0;
    const char *data = PyUnicode_AsUTF8AndSize(s, &size);
    if (data == nullptr)
        throw PyError{};
    return { data, size_t(size) };
}

// -----------------------------
template <class E, size_t N>
E enum_value(const std::array<const char*, N> &names, PyObject *name, const char *what)
{
    const auto value = text(name, what);
    for (size_t i = 0; i < N; ++i)
        if (value == names[i])
            return E(i);
    throw TypeException{ std::string("Invalid ") + what + " '" + std::string(value) + "'" };
}

// -----------------------------
TimeStamp timestamp(PyObject *ts)
{
    const unsigned long long value = PyLong_AsUnsignedLongLong(ts);  // negative: OverflowError
    if (value == static_cast<unsigned long long>(-1) && PyErr_Occurred())
        throw PyError{};
    return value;
}

// -----------------------------
HeadPtr make_py_header(std::string_view timescale, PyObject *date, const char *comment, const char *version)
{
    int number = 0;
    const char *end = timescale.data() + timescale.size();
    const auto [unit_begin, ec] = std::from_chars(timescale.data(), end, number);
    if (ec != std::errc{} || number > int(TimeScale::hundred))  // TimeScale is a char: no wrap-around
        throw TypeException{ "Invalid timescale '" + std::string(timescale) + "'" };
    std::string_view unit(unit_begin, size_t(end - unit_begin));
    while (!unit.empty() && unit.front() == ' ')
        unit.remove_prefix(1);
    const Ref unit_str{ check(PyUnicode_FromStringAndSize(unit.data(), Py_ssize_t(unit.size()))) };
    const auto unit_value = enum_value<TimeScaleUnit>(TIMESCALE_UNITS, unit_str.get(), "timescale unit");
    const std::string date_value = (date == Py_None) ? utils::now() : std::string(text(date, "date"));
    return make_header(TimeScale(number), unit_value, date_value, comment, version);
}

// -----------------------------
const VarValue& to_value(const Variable &var, PyObject *value, std::string &buf)
{
    using Kind = Variable::Kind;
    buf.clear();
    if (PyUnicode_Check(value))
        buf.assign(text(value, "value"));
    else if (value == Py_None && (var.kind == Kind::scalar || var.kind == Kind::vector))
        buf = char(Bit::highz);
    else if (PyBool_Check(value) && var.kind != Kind::real && var.kind != Kind::string)
        buf = (value == Py_True) ? char(Bit::one) : char(Bit::zero);
    else if (PyLong_Check(value) && var.kind != Kind::real && var.kind != Kind::string)
        int_to_bits(var, value, buf);
    else if ((PyFloat_Check(value) || PyLong_Check(value)) && var.kind == Kind::real)
    {
        const double d = PyFloat_AsDouble(value);
        if (d == -1.0 && PyErr_Occurred())
            throw PyError{};
        std::array<char, DOUBLE_TEXT_SIZE> chars{};
        buf.assign(chars.data(), std::to_chars(chars.data(), chars.data() + chars.size(), d).ptr);
    }
    else
        throw TypeException{ std::string("Invalid value of type '") + Py_TYPE(value)->tp_name +
                             "' of var '" + var.name + "'" };
    return buf;
}

// -----------------------------
void append_bits(std::string &out, unsigned long long value, unsigned bits)
{
    if (bits < INT_BITS)
        value &= (1ULL << bits) - 1;
    for (int i = std::max(std::bit_width(value), 1); i-- > 0;)
        out += ((value >> i) & 1u) ? char(Bit::one) : char(Bit::zero);
}

// -----------------------------
void int_to_bits(const Variable &var, PyObject *value, std::string &out)
{
    int overflow = 0;
    const long long v = PyLong_AsLongLongAndOverflow(value, &overflow);
    if (v == -1 && PyErr_Occurred())
        throw PyError{};
    if (overflow == 0 && (v >= 0 || var.size <= INT_BITS))
    {   // a too long positive value is rejected by the writer
        if (v < 0 && var.size < INT_BITS && v < -(1LL << (var.size - 1)))
            throw TypeException{ "Invalid value " + std::to_string(v) + " of var '" + var.name + "'" };
        append_bits(out, static_cast<unsigned long long>(v), (v < 0) ? var.size : INT_BITS);
        return;
    }
    // wider than 64 bits: Python formats it
    Ref number{ check(PyNumber_Long(value)) };
    if (overflow < 0 || (overflow == 0 && v < 0))
    {   // -2**(size-1) <= value: two's complement is value + 2**size
        const Ref one{ check(PyLong_FromLong(1)) };
        const Ref shift{ check(PyLong_FromUnsignedLong(var.size - 1)) };
        const Ref half{ check(PyNumber_Lshift(one.get(), shift.get())) };
        const Ref min{ check(PyNumber_Negative(half.get())) };
        const int below = PyObject_RichCompareBool(number.get(), min.get(), Py_LT);
        if (below < 0)
            throw PyError{};
        if (below)
            throw TypeException{ "Invalid negative value of var '" + var.name + "'" };
        const Ref modulus{ check(PyNumber_Add(half.get(), half.get())) };
        number.reset(check(PyNumber_Add(number.get(), modulus.get())));
    }
    const Ref format{ check(PyUnicode_FromString("b")) };
    const Ref bits{ check(PyObject_Format(number.get(), format.get())) };
    out += text(bits.get(), "bits");
}

// -----------------------------
PyObject* make_variable(VarPtr var)
{
    auto *obj = PyObject_New(VariableObject, variable_type);
    if (obj == nullptr)
        throw PyError{};
    new (&obj->var) VarPtr(std::move(var));
    return reinterpret_cast<PyObject*>(obj);
}

// -----------------------------
void variable_dealloc(PyObject *self)
{
    PyTypeObject *type = Py_TYPE(self);
    reinterpret_cast<VariableObject*>(self)->var.~VarPtr();
    type->tp_free(self);
    Py_DECREF(type);  // instances of heap types own a reference to it
}

// -----------------------------
PyObject* variable_new(PyTypeObject*, PyObject*, PyObject*)
{
    PyErr_SetString(PyExc_TypeError, "Variables are made by Writer.register_var()");
    return nullptr;
}

// -----------------------------
PyObject* variable_repr(PyObject *self)
{
    const auto &v = variable_of(self);
    return PyUnicode_FromFormat("<vcdwriter.Variable %s %u %s>", VAR_TYPES[size_t(v.type)], v.size, v.name.c_str());
}

// -----------------------------
PyObject* variable_name(PyObject *self, void*) { return PyUnicode_FromString(variable_of(self).name.c_str()); }
PyObject* variable_vtype(PyObject *self, void*) { return PyUnicode_FromString(VAR_TYPES[size_t(variable_of(self).type)]); }
PyObject* variable_size(PyObject *self, void*) { return PyLong_FromUnsignedLong(variable_of(self).size); }
PyObject* variable_ident(PyObject *self, void*) { return PyLong_FromUnsignedLong(variable_of(self).ident); }

// -----------------------------
int writer_init(PyObject *self, PyObject *args, PyObject *kwargs)
{
    static const char *keywords[] = { "file", "timescale", "date", "comment", "version", "init_timestamp", nullptr };
    PyObject *file = nullptr, *date = Py_None, *init_timestamp = nullptr;
    const char *timescale = "1 us", *comment = "", *version = "";
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|sOssO:Writer", const_cast<char**>(keywords),
                                     &file, &timescale, &date, &comment, &version, &init_timestamp))
        return -1;
    auto *obj = reinterpret_cast<WriterObject*>(self);
    const PyObject *result = guarded([&]() -> PyObject* {
        if (obj->state != nullptr)
            throw Exception{ "The writer is initialized already" };
        const Ref path{ check(PyOS_FSPath(file)) };
        Ref path_str{ path.get() };
        Py_INCREF(path_str.get());
        if (PyBytes_Check(path.get()))
            path_str.reset(check(PyUnicode_DecodeFSDefault(PyBytes_AS_STRING(path.get()))));
        const TimeStamp ts = init_timestamp ? timestamp(init_timestamp) : 0;
        obj->state = new WriterState{ Writer(std::string(text(path_str.get(), "file")),
                                             make_py_header(timescale, date, comment, version), ts), {} };
        return Py_None;
    });
    return result ? 0 : -1;
}

// -----------------------------
void writer_dealloc(PyObject *self)
{
    PyTypeObject *type = Py_TYPE(self);
    delete reinterpret_cast<WriterObject*>(self)->state;  // closes the file, never throws
    type->tp_free(self);
    Py_DECREF(type);
}

// -----------------------------
WriterState& state_of(PyObject *self)
{
    auto *state = reinterpret_cast<WriterObject*>(self)->state;
    if (state == nullptr)
        throw Exception{ "The writer is not initialized" };
    return *state;
}

// -----------------------------
PyObject* writer_register_var(PyObject *self, PyObject *args, PyObject *kwargs)
{
    static const char *keywords[] = { "scope", "name", "var_type", "size", "init", nullptr };
    const char *scope = nullptr, *name = nullptr;
    PyObject *var_type = nullptr, *size_obj = nullptr, *init = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ssO|OO:register_var", const_cast<char**>(keywords),
                                     &scope, &name, &var_type, &size_obj, &init))
        return nullptr;
    return guarded([&] {
        auto &s = state_of(self);
        const auto type = enum_value<VariableType>(VAR_TYPES, var_type, "var type");
        const unsigned long size = size_obj ? PyLong_AsUnsignedLong(size_obj) : 0;
        if (size == static_cast<unsigned long>(-1) && PyErr_Occurred())
            throw PyError{};
        if (size > UINT_MAX)
            throw TypeException{ "Invalid size of var '" + std::string(name) + "'" };
        if (init == Py_None)
            return make_variable(s.writer.register_var(scope, name, type, unsigned(size)));
        // *init* is converted by the rules of the variable: validate them on a temporary one
        const Variable probe(name, type, unsigned(size), 0);
        return make_variable(s.writer.register_var(scope, name, type, unsigned(size), to_value(probe, init, s.buf)));
    });
}

// -----------------------------
PyObject* writer_change(PyObject *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    return guarded([&] {
        auto &s = state_of(self);
        if (kwnames != nullptr && PyTuple_GET_SIZE(kwnames) != 0)
            throw TypeException{ "change() takes positional arguments only" };
        if (nargs == 3 && Py_TYPE(args[0]) == variable_type)
        {   // change(var, timestamp, value): the hot path
            const auto &var = reinterpret_cast<VariableObject*>(args[0])->var;
            return boolean(s.writer.change(var, timestamp(args[1]), to_value(*var, args[2], s.buf)));
        }
        if (nargs == 4)
        {   // change(scope, name, timestamp, value)
            const VarPtr var = s.writer.var(std::string(text(args[0], "scope")), std::string(text(args[1], "name")));
            return boolean(s.writer.change(var, timestamp(args[2]), to_value(*var, args[3], s.buf)));
        }
        throw TypeException{ "change() takes (var, timestamp, value) or (scope, name, timestamp, value)" };
    });
}

// -----------------------------
PyObject* writer_var(PyObject *self, PyObject *args, PyObject *kwargs)
{
    static const char *keywords[] = { "scope", "name", nullptr };
    const char *scope = nullptr, *name = nullptr;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ss:var", const_cast<char**>(keywords), &scope, &name))
        return nullptr;
    return guarded([&] { return make_variable(state_of(self).writer.var(scope, name)); });
}

// -----------------------------
// Parse the only *keyword* argument, call *f* with it
template <class F>
PyObject* one_arg(PyObject *args, PyObject *kwargs, const char *format, const char *keyword, PyObject *arg, F &&f)
{
    const char *keywords[] = { keyword, nullptr };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, format, const_cast<char**>(keywords), &arg))
        return nullptr;
    return guarded([&] { f(arg); return none(); });
}

PyObject* writer_dump_off(PyObject *self, PyObject *args, PyObject *kwargs)
{
    return one_arg(args, kwargs, "O:dump_off", "timestamp", nullptr,
                   [&](PyObject *ts) { state_of(self).writer.dump_off(timestamp(ts)); });
}

PyObject* writer_dump_on(PyObject *self, PyObject *args, PyObject *kwargs)
{
    return one_arg(args, kwargs, "O:dump_on", "timestamp", nullptr,
                   [&](PyObject *ts) { state_of(self).writer.dump_on(timestamp(ts)); });
}

PyObject* writer_flush(PyObject *self, PyObject *args, PyObject *kwargs)
{
    return one_arg(args, kwargs, "|O:flush", "timestamp", Py_None, [&](PyObject *ts) {
        const TimeStamp value = (ts == Py_None) ? 0 : timestamp(ts);
        state_of(self).writer.flush((ts == Py_None) ? nullptr : &value);
    });
}

PyObject* writer_close(PyObject *self, PyObject *args, PyObject *kwargs)
{
    return one_arg(args, kwargs, "|O:close", "timestamp", Py_None, [&](PyObject *ts) {
        const TimeStamp value = (ts == Py_None) ? 0 : timestamp(ts);
        state_of(self).writer.close((ts == Py_None) ? nullptr : &value);
    });
}

PyObject* writer_set_scope_default_type(PyObject *self, PyObject *args, PyObject *kwargs)
{
    return one_arg(args, kwargs, "O:set_scope_default_type", "scope_type", nullptr, [&](PyObject *type) {
        state_of(self).writer.set_scope_default_type(enum_value<ScopeType>(SCOPE_TYPES, type, "scope type"));
    });
}

PyObject* writer_set_scope_sep(PyObject *self, PyObject *args, PyObject *kwargs)
{
    return one_arg(args, kwargs, "O:set_scope_sep", "scope_sep", nullptr, [&](PyObject *sep) {
        state_of(self).writer.set_scope_sep(std::string(text(sep, "scope_sep")));
    });
}

// -----------------------------
PyObject* writer_set_scope_type(PyObject *self, PyObject *args, PyObject *kwargs)
{
    static const char *keywords[] = { "scope", "scope_type", nullptr };
    const char *scope = nullptr;
    PyObject *type = nullptr;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sO:set_scope_type", const_cast<char**>(keywords), &scope, &type))
        return nullptr;
    return guarded([&] {
        state_of(self).writer.set_scope_type(scope, enum_value<ScopeType>(SCOPE_TYPES, type, "scope type"));
        return none();
    });
}

// -----------------------------
PyObject* writer_enter(PyObject *self, PyObject*)
{
    Py_INCREF(self);
    return self;
}

PyObject* writer_exit(PyObject *self, PyObject*)
{
    return guarded([&] { state_of(self).writer.close(); return none(); });
}

// -----------------------------
// A C function of any signature as a PyCFunction (no cast-function-type warning)
template <class F>
PyCFunction method(F f) { return reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(f)); }

constexpr int KW = METH_VARARGS | METH_KEYWORDS;

PyMethodDef writer_methods[] = {
    { "register_var", method(writer_register_var), KW,
      "register_var(scope, name, var_type, size=0, init=None) -> Variable\n"
      "Register a variable; size 0 is the default size of integer, time, real, realtime, string, event." },
    { "change", method(writer_change), METH_FASTCALL | METH_KEYWORDS,
      "change(var, timestamp, value) -> bool\nchange(scope, name, timestamp, value) -> bool\n"
      "Change the value: str, bool, int, float or None ('z').\n"
      "Return True if the value is written, False if it equals the previous one." },
    { "var", method(writer_var), KW, "var(scope, name) -> Variable\nThe variable *name* in *scope*." },
    { "dump_off", method(writer_dump_off), KW, "dump_off(timestamp)\nSuspend dumping ($dumpoff)." },
    { "dump_on", method(writer_dump_on), KW, "dump_on(timestamp)\nResume dumping ($dumpon)." },
    { "flush", method(writer_flush), KW, "flush(timestamp=None)\nWrite the buffered output to the file." },
    { "close", method(writer_close), KW, "close(timestamp=None)\nFlush and close the file; a repeated call does nothing." },
    { "set_scope_type", method(writer_set_scope_type), KW, "set_scope_type(scope, scope_type)\nSet the type of an existing scope." },
    { "set_scope_default_type", method(writer_set_scope_default_type), KW,
      "set_scope_default_type(scope_type)\nType of the scopes registered further (default: 'module')." },
    { "set_scope_sep", method(writer_set_scope_sep), KW, "set_scope_sep(scope_sep)\nSeparator of the scope names (default: '.')." },
    { "__enter__", method(writer_enter), METH_NOARGS, nullptr },
    { "__exit__", method(writer_exit), METH_VARARGS, "Close the file." },
    { nullptr, nullptr, 0, nullptr },
};

PyGetSetDef variable_getset[] = {
    { "name", variable_name, nullptr, "Name in the scope.", nullptr },
    { "type", variable_vtype, nullptr, "Verilog data type.", nullptr },
    { "size", variable_size, nullptr, "Size in bits.", nullptr },
    { "ident", variable_ident, nullptr, "Identifier in the VCD output.", nullptr },
    { nullptr, nullptr, nullptr, nullptr, nullptr },
};

PyType_Slot writer_slots[] = {
    { Py_tp_doc, const_cast<char*>(
        "Writer(file, timescale='1 us', date=None, comment='', version='', init_timestamp=0)\n"
        "Writer of a Value Change Dump file (created or truncated).\n"
        "timescale: '<1|10|100> <s|ms|us|ns|ps|fs>'; date: 'YYYY-MM-DD hh:mm:ss', None (now) or '' (no $date).") },
    { Py_tp_new, reinterpret_cast<void*>(PyType_GenericNew) },
    { Py_tp_init, reinterpret_cast<void*>(writer_init) },
    { Py_tp_dealloc, reinterpret_cast<void*>(writer_dealloc) },
    { Py_tp_methods, writer_methods },
    { 0, nullptr },
};

PyType_Slot variable_slots[] = {
    { Py_tp_doc, const_cast<char*>("Handle of a registered variable.") },
    { Py_tp_new, reinterpret_cast<void*>(variable_new) },
    { Py_tp_dealloc, reinterpret_cast<void*>(variable_dealloc) },
    { Py_tp_repr, reinterpret_cast<void*>(variable_repr) },
    { Py_tp_getset, variable_getset },
    { 0, nullptr },
};

PyType_Spec writer_spec{ "vcdwriter.Writer", sizeof(WriterObject), 0, unsigned(Py_TPFLAGS_DEFAULT), writer_slots };
PyType_Spec variable_spec{ "vcdwriter.Variable", sizeof(VariableObject), 0, unsigned(Py_TPFLAGS_DEFAULT), variable_slots };

PyModuleDef module_def{
    PyModuleDef_HEAD_INIT, "vcdwriter",
    "Writer of Value Change Dump (VCD) files, IEEE 1364-2005, 18.2 (C++ vcd-writer).",
    -1, nullptr, nullptr, nullptr, nullptr, nullptr };

// Add *obj* to *module* as *name* (steals the reference)
void add(PyObject *module, const char *name, PyObject *obj)
{
    if (obj == nullptr || PyModule_AddObject(module, name, obj) < 0)
    {
        Py_XDECREF(obj);
        throw PyError{};
    }
}
}
}

PyMODINIT_FUNC PyInit_vcdwriter()
{
    using namespace vcd;
    Ref module{ PyModule_Create(&module_def) };
    if (!module)
        return nullptr;
    const PyObject *ok = guarded([&] {
        vcd_error = check(PyErr_NewException("vcdwriter.VCDError", nullptr, nullptr));
        phase_error = check(PyErr_NewException("vcdwriter.VCDPhaseError", vcd_error, nullptr));
        const Ref bases{ check(PyTuple_Pack(2, vcd_error, PyExc_ValueError)) };
        type_error = check(PyErr_NewException("vcdwriter.VCDTypeError", bases.get(), nullptr));
        variable_type = reinterpret_cast<PyTypeObject*>(check(PyType_FromSpec(&variable_spec)));
        // the module keeps a reference of each object, the globals keep another one
        for (auto [name, obj] : { std::pair{ "VCDError", vcd_error }, std::pair{ "VCDPhaseError", phase_error },
                                  std::pair{ "VCDTypeError", type_error },
                                  std::pair{ "Variable", reinterpret_cast<PyObject*>(variable_type) } })
        {
            Py_INCREF(obj);
            add(module.get(), name, obj);
        }
        add(module.get(), "Writer", PyType_FromSpec(&writer_spec));
        return module.get();
    });
    return ok ? module.release() : nullptr;
}
