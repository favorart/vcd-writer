#include "vcd_writer.h"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <unordered_map>
#include <vector>

#include "vcd_header.h"
#include "vcd_utils.h"


// -----------------------------
namespace vcd {
using namespace utils;

namespace {
constexpr std::array<const char*, size_t(ScopeType::_count_)> SCOPE_TYPES{ "begin", "fork", "function", "module", "task" };
constexpr int TIMESTAMP_BASE = 10;  // "#<timestamp>" is decimal
constexpr int IDENT_BASE = 16;      // identifiers are hexadecimal, the same as in "$var"
}

// -----------------------------
struct Scope
{
    ScopeType type;
    std::vector<const Variable*> vars;
};

// -----------------------------
// VCD variable details needed to call :meth:`Writer.change()`.
class Variable
{
public:
    Variable() = delete;
    Variable(Variable&&) = delete;
    Variable& operator=(Variable&&) = delete;
    Variable(const Variable&) = delete;
    Variable& operator=(const Variable&) = delete;

protected:
    Variable(std::string name, VariableType type, unsigned size, unsigned ident) :
        _ident(ident), _type(type), _name(std::move(name)), _size(size) {}

    unsigned     _ident;  // internal ID used in VCD output stream
    VariableType _type;   // VCD variable type, one of `VariableTypes`
    std::string  _name;   // human-readable name
    unsigned     _size;   // size of variable, in bits

    //! string representation of variable types
    static const std::array<const char*, 19> VAR_TYPES;
    static_assert(int(VariableType::wor) + 1 == 19, "VAR_TYPES must match VariableType");

public:
    virtual ~Variable() = default;

    //! string representation of variable declartion in VCD
    [[nodiscard]] std::string declartion() const;
    //! string representation of value change record in VCD, written into *record*
    //! (a reused buffer: no memory allocation once its capacity is enough;
    //!  its contents are unspecified if the value is invalid and an exception is thrown)
    virtual void change_record(const VarValue &value, VarValue &record) const = 0;

    friend class Writer;
};

// -----------------------------
const std::array<const char*, 19> Variable::VAR_TYPES = {
    "wire", "reg", "string", "parameter", "integer", "real", "realtime", "time", "event",
    "supply0", "supply1", "tri", "triand", "trior", "trireg", "tri0", "tri1", "wand", "wor"
};

// -----------------------------
// One-bit VCD scalar is a 4-state variable and thus may have one of
// `Bit`. An empty *value* is the same as `Bit::undef`
struct ScalarVariable : public Variable
{
    ScalarVariable(const std::string &name, VariableType type, unsigned size, unsigned ident) :
        Variable(name, type, size, ident) {}
    void change_record(const VarValue &value, VarValue &record) const override
    {
        char c = Bit::undef;
        if (!value.empty())
        {
            c = to_lower(value[0]);
            if (value.size() != 1 || (c != Bit::one   && c != Bit::zero
                                   && c != Bit::undef && c != Bit::highz))
                throw TypeException{ std::format("Invalid scalar value '{}'", value) };
        }
        record.assign(1, c);
    }
};

// -----------------------------
// String variable as known by GTKWave. Any `string` (character-chain)
// can be displayed as a change.This type is only supported by GTKWave.
// The value must be non-empty and must not contain whitespace or control characters.
struct StringVariable : public Variable
{
    StringVariable(const std::string &name, VariableType type, unsigned size, unsigned ident) :
        Variable(name, type, size, ident) {}
    void change_record(const VarValue &value, VarValue &record) const override
    {
        if (value.empty() || !std::all_of(value.begin(), value.end(), is_graph))
            throw TypeException{ std::format("Invalid string value '{}'", value) };
        record.clear();
        record += 's';
        record += value;
        record += ' ';
    }
};

// -----------------------------
// Real (IEEE-754 double-precision floating point) variable. Values must
// be numeric and can't be `Bit::undef` or `Bit::highz` states
struct RealVariable : public Variable
{
    RealVariable(const std::string &name, VariableType type, unsigned size, unsigned ident) :
        Variable(name, type, size, ident) {}
    void change_record(const VarValue &value, VarValue &record) const override
    {
        double d = 0.0;
        size_t idx = 0;
        try { d = std::stod(value, &idx); }
        catch (const std::exception&) { idx = 0; }
        if (idx == 0 || idx != value.size())
            throw TypeException{ std::format("Invalid real value '{}'", value) };
        record.clear();
        std::format_to(std::back_inserter(record), "r{:.16g} ", d);
    }
};

// -----------------------------
// Bit vector variable type for the various non-scalar and non-real
// variable types, including integer, register, wire, etc.
struct VectorVariable : public Variable
{
    VectorVariable(const std::string &name, VariableType type, unsigned size, unsigned ident) :
        Variable(name, type, size, ident) {}
    void change_record(const VarValue &value, VarValue &record) const override;
};

// -----------------------------
// State of a writer and its helpers
struct Writer::Impl
{
    Impl(const std::string &filename, HeadPtr head, TimeStamp init_timestamp);

    std::ofstream file;
    HeadPtr       header;  // until it is written

    // settings
    std::string scope_sep = ".";
    ScopeType   scope_def_type = ScopeType::module;

    // state
    TimeStamp timestamp;
    bool timestamp_printed = false;  // "#<timestamp>" is written
    bool registering = true;         // the header is not written yet
    bool dumping = true;
    bool closed = false;

    std::map<std::string, Scope> scopes;             // scope -> its variables
    std::unordered_map<std::string, VarPtr> vars;    // scope + '\0' + name -> variable
    std::vector<VarPtr> idents;                      // ident -> variable
    std::vector<VarValue> values;                    // ident -> last value (change record)

    // reused buffers: no memory allocation per change in a steady state
    std::string buf;     // output not written to the file yet
    VarValue    record;  // change record of the current change
    std::string key;     // lookup key of change(scope, name)
    static constexpr size_t buf_limit = 64 * 1024;

    // throws PhaseException after close()
    void check_open(const char *what) const;
    // registered variable (throws PhaseException)
    const VarPtr& find_var(const std::string &scope, const std::string &name, std::string &var_key) const;
    // move the current time to *ts*, the header is written on the first move
    void advance(TimeStamp ts);
    void set_dumping(bool on, TimeStamp ts);

    // output: formatted into the buffer, written to the file when the buffer is full
    template <class... Args>
    void print(std::format_string<Args...> fmt, Args&&... args);
    // "<record><ident>\n", the hot path: no std::format
    void print_change(const VarValue &value, unsigned ident);
    // "#<timestamp>\n" once per timestamp
    void print_timestamp();
    void write_buf_if_full() { if (buf.size() >= buf_limit) write_buf(); }
    void write_buf();

    // "<keyword>" and the current values of all variables
    void dump_values(const char *keyword);
    // "$dumpoff" and 'x' for all variables that have such a state
    void dump_off_values();
    void write_header();
    // write the header and the initial values, no more registration
    void finalize_registration();
};

// -----------------------------
Writer::Impl::Impl(const std::string &filename, HeadPtr head, TimeStamp init_timestamp) :
    file(filename, std::ios::out | std::ios::trunc | std::ios::binary),
    header(head ? std::move(head) : make_header()),
    timestamp(init_timestamp)
{
    if (!file)
        throw Exception{ std::format("Cannot open file '{}'", filename) };
    file.exceptions(std::ios::badbit | std::ios::failbit); // report write errors as exceptions
}

// -----------------------------
void Writer::Impl::check_open(const char *what) const
{
    if (closed)
        throw PhaseException{ std::format("Cannot {} after close()", what) };
}

// -----------------------------
Writer::Writer(std::string filename, HeadPtr &&header, TimeStamp init_timestamp) :
    _impl(std::make_unique<Impl>(filename, std::move(header), init_timestamp))
{}

// -----------------------------
Writer::~Writer()
{
    try { close(); }
    catch (...) {} // destructor must not throw
}

// -----------------------------
VarPtr Writer::register_var(const std::string &scope, const std::string &name, VariableType type,
                               unsigned size, const VarValue &init)
{
    auto &d = *_impl;
    d.check_open("register a variable");
    if (!d.registering)
        throw PhaseException{ std::format("Cannot register new var '{}', registering finished", name) };
    if (!is_valid_token(name))
        throw TypeException{ std::format("Invalid var name '{}'", name) };
    for (const auto &part : split(scope, d.scope_sep))
        if (!is_valid_token(part))
            throw TypeException{ std::format("Invalid scope '{}'", scope) };

    std::string var_key;
    make_var_key(var_key, scope, name);
    if (d.vars.find(var_key) != d.vars.end())
        throw TypeException{ std::format("Duplicate var '{}' in scope '{}'", name, scope) };

    const auto ident = static_cast<unsigned>(d.idents.size());
    auto sz = [&size](unsigned def) { return (size ? size : def);  };

    VarPtr pvar;
    VarValue init_value(init);
    switch (type)
    {
        case VariableType::integer:
        case VariableType::time:
            if (sz(64) == 1)
                pvar = VarPtr(new ScalarVariable(name, type, 1, ident));
            else
                pvar = VarPtr(new VectorVariable(name, type, sz(64), ident));
            break;

        case VariableType::real:
        case VariableType::realtime:
            pvar = VarPtr(new RealVariable(name, type, sz(64), ident));
            if (init_value.size() == 1 && init_value[0] == Bit::undef)
                init_value = "0.0";
            break;

        case VariableType::string:
            pvar = VarPtr(new StringVariable(name, type, sz(1), ident));
            break;

        case VariableType::event:
            pvar = VarPtr(new ScalarVariable(name, type, 1, ident));
            break;

        default:
            if (!size)
                throw TypeException{ std::format("Must supply size for type '{}' of var '{}'",
                                               Variable::VAR_TYPES[int(type)], name) };
            pvar = VarPtr(new VectorVariable(name, type, size, ident));
            break;
    }
    // events have no value, other vars validate the initial value
    VarValue init_record;
    if (type != VariableType::event)
        pvar->change_record(init_value, init_record);

    // only alter the state after all checks succeeded
    d.scopes.try_emplace(scope, Scope{ d.scope_def_type, {} }).first->second.vars.push_back(pvar.get());
    d.vars.emplace(std::move(var_key), pvar);
    d.idents.push_back(pvar);
    d.values.push_back(std::move(init_record));
    return pvar;
}

// -----------------------------
void Writer::Impl::advance(TimeStamp ts)
{
    if (ts < timestamp)
        throw PhaseException{ std::format("Out of order timestamp {} < {}", ts, timestamp) };
    if (ts == timestamp)
        return;
    if (registering)
        finalize_registration();
    timestamp = ts;
    timestamp_printed = false;
}

// -----------------------------
void Writer::Impl::print_timestamp()
{
    if (timestamp_printed)
        return;
    buf += '#';
    append_number(buf, timestamp, TIMESTAMP_BASE);
    buf += '\n';
    timestamp_printed = true;
    write_buf_if_full();
}

// -----------------------------
template <class... Args>
void Writer::Impl::print(std::format_string<Args...> fmt, Args&&... args)
{
    std::format_to(std::back_inserter(buf), fmt, std::forward<Args>(args)...);
    write_buf_if_full();
}

// -----------------------------
void Writer::Impl::print_change(const VarValue &value, unsigned ident)
{
    buf += value;
    append_number(buf, ident, IDENT_BASE);
    buf += '\n';
    write_buf_if_full();
}

// -----------------------------
void Writer::Impl::write_buf()
{
    // the buffer is cleared even on a write error: the error is reported once
    std::string out;
    out.swap(buf);
    file.write(out.data(), std::streamsize(out.size()));
    out.clear();
    buf.swap(out); // keep the capacity
}

// -----------------------------
bool Writer::change(const VarPtr &var, TimeStamp timestamp, const VarValue &value)
{
    auto &d = *_impl;
    if (!var)
        throw TypeException{ "Invalid Variable" };
    d.check_open("change a value");
    if (timestamp < d.timestamp)
        throw PhaseException{ std::format("Out of order value change var '{}'", var->_name) };
    if (var->_ident >= d.idents.size() || d.idents[var->_ident] != var)
        throw TypeException{ std::format("Variable '{}' do not registered", var->_name) };

    var->change_record(value, d.record); // validate before any state change
    d.advance(timestamp);

    if (var->_type == VariableType::event)
    {   // events are not values: every change triggers the event
        if (d.registering)
            d.finalize_registration();
        if (d.dumping)
        {
            d.print_timestamp();
            d.print_change(d.record, var->_ident);
        }
        return true;
    }

    auto &prev = d.values[var->_ident];
    if (prev == d.record)
        return false;
    prev.swap(d.record); // the old value's buffer is reused by the next change

    // during registration the value goes to "$dumpvars"
    if (d.dumping && !d.registering)
    {
        d.print_timestamp();
        d.print_change(prev, var->_ident);
    }
    return true;
}

// -----------------------------
bool Writer::change(const std::string &scope, const std::string &name, TimeStamp timestamp, const VarValue &value)
{
    return change(_impl->find_var(scope, name, _impl->key), timestamp, value);
}

// -----------------------------
const VarPtr& Writer::Impl::find_var(const std::string &scope, const std::string &name, std::string &var_key) const
{
    make_var_key(var_key, scope, name);
    const auto it = vars.find(var_key);
    if (it == vars.end())
        throw PhaseException{ std::format("The var '{}' in scope '{}' does not exist", name, scope) };
    return it->second;
}

// -----------------------------
VarPtr Writer::var(const std::string &scope, const std::string &name) const
{
    std::string var_key;
    return _impl->find_var(scope, name, var_key);
}

// -----------------------------
void Writer::set_scope_type(const std::string &scope, ScopeType type)
{
    const auto it = _impl->scopes.find(scope);
    if (it == _impl->scopes.end())
        throw PhaseException{ std::format("Such scope '{}' does not exist", scope) };
    it->second.type = type;
}

// -----------------------------
void Writer::set_scope_default_type(ScopeType type) { _impl->scope_def_type = type; }

// -----------------------------
void Writer::set_scope_sep(const std::string &scope_sep)
{
    if (!scope_sep.empty())
        _impl->scope_sep = scope_sep;
}

// -----------------------------
void Writer::Impl::set_dumping(bool on, TimeStamp ts)
{
    check_open(on ? "dump_on()" : "dump_off()");
    if (registering && ts == timestamp)
    {   // the header is not written yet, `finalize_registration()` handles it
        dumping = on;
        return;
    }
    advance(ts);
    if (dumping == on)
        return;
    dumping = on;
    print_timestamp();
    if (on)
        dump_values("$dumpon");
    else
        dump_off_values();
}

// -----------------------------
void Writer::dump_off(TimeStamp timestamp) { _impl->set_dumping(false, timestamp); }
void Writer::dump_on(TimeStamp timestamp) { _impl->set_dumping(true, timestamp); }

// -----------------------------
void Writer::flush(const TimeStamp *timestamp)
{
    auto &d = *_impl;
    d.check_open("flush()");
    if (timestamp != nullptr && *timestamp > d.timestamp)
    {
        d.advance(*timestamp);
        if (d.dumping)
            d.print_timestamp();
    }
    if (d.registering)
        d.finalize_registration();
    d.write_buf();
    d.file.flush();
}

// -----------------------------
void Writer::close(const TimeStamp *timestamp)
{
    auto &d = *_impl;
    if (d.closed)
        return;
    try
    {
        flush(timestamp);
    }
    catch (...)
    {
        d.closed = true;
        try { d.file.close(); } catch (...) {}
        throw;
    }
    d.closed = true;
    d.file.close();
}

// -----------------------------
void Writer::Impl::dump_off_values()
{
    print("$dumpoff\n");
    for (const auto &var : idents)
    {
        const auto ident = var->_ident;
        const auto &value = values[ident];

        if (value.empty() || value[0] == 'r' || value[0] == 's')
        {} // events, real and string variables cannot have "x" state
        else if (value[0] == 'b')
        { print("bx {:x}\n", ident); }
        else
        { print("x{:x}\n", ident); }
    }
    print("$end\n");
}

// -----------------------------
void Writer::Impl::dump_values(const char *keyword)
{
    print("{}\n", keyword);
    for (const auto &var : idents)
        if (!values[var->_ident].empty()) // events have no value
            print_change(values[var->_ident], var->_ident);
    print("$end\n");
}

// -----------------------------
void Writer::Impl::write_header()
{
    for (size_t i = 0; i < header->values.size(); ++i)
        if (!header->values[i].empty())
            print("{} {} $end\n", Header::keywords[i], indent_lines(header->values[i]));
    header.reset();

    // sort scopes by their hierarchical path, so a scope is followed by its children
    std::vector<std::pair<std::vector<std::string>, const Scope*>> sorted;
    sorted.reserve(scopes.size());
    for (const auto &[name, scope] : scopes)
        sorted.emplace_back(split(name, scope_sep), &scope);
    std::sort(sorted.begin(), sorted.end(),
              [](const auto &l, const auto &r) { return l.first < r.first; });

    std::vector<std::string> opened;
    for (const auto &[path, scope] : sorted)
    {
        // close the scopes which are not the ancestors of the current one
        const auto common = std::mismatch(opened.begin(), opened.end(), path.begin(), path.end()).first;
        for (auto n = opened.end() - common; n > 0; --n)
            print("$upscope $end\n");
        opened.erase(common, opened.end());
        // open the (intermediate) scopes
        for (size_t i = opened.size(); i < path.size(); ++i)
        {
            const auto type = (i + 1 == path.size()) ? scope->type : scope_def_type;
            print("$scope {} {} $end\n", SCOPE_TYPES[size_t(type)], path[i]);
            opened.push_back(path[i]);
        }
        for (const auto *var : scope->vars)
            print("{}\n", var->declartion());
    }
    for (size_t i = 0; i < opened.size(); ++i)
        print("$upscope $end\n");
    print("$enddefinitions $end\n");
}

// -----------------------------
void Writer::Impl::finalize_registration()
{
    write_header();
    registering = false;
    if (idents.empty())
        return;
    print_timestamp();
    dump_values("$dumpvars");
    if (!dumping)
        dump_off_values();
}

// -----------------------------
std::string Variable::declartion() const
{
    return std::format("$var {} {} {:x} {} $end", VAR_TYPES[int(_type)], _size, _ident, _name);
}

// -----------------------------
//  :Warning: *value* is string where all characters must be one of `Bit`.
//  An empty  *value* is the same as `Bit::undef`.
//  A shorter *value* is left-extended: with 'x'/'z' if its leftmost bit is 'x'/'z',
//  with '0' otherwise (IEEE 1364-2005, 18.2.1).
void VectorVariable::change_record(const VarValue &value, VarValue &record) const
{
    if (value.size() > _size)
        throw TypeException{ std::format("Invalid binary vector value '{}' size '{}'", value, _size) };

    // "b<_size bits> ", written in place (*record* is a scratch buffer, garbage on throw)
    record.resize(size_t(_size) + 2);
    char *p = record.data();
    *p++ = 'b';
    if (value.empty())
        p = std::fill_n(p, _size, char(Bit::undef));
    else
    {
        const char first = to_lower(value[0]);
        const char fill = (first == Bit::undef || first == Bit::highz) ? first : char(Bit::zero);
        p = std::fill_n(p, _size - value.size(), fill);
        for (const char ch : value)
        {
            const char c = to_lower(ch);
            if (c != Bit::one && c != Bit::zero && c != Bit::undef && c != Bit::highz)
                throw TypeException{ std::format("Invalid binary vector value '{}' size '{}'", value, _size) };
            *p++ = c;
        }
    }
    *p = ' ';
}

// -----------------------------
} //end namespace vcd

