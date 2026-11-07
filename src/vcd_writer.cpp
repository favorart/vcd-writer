#include <string>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <array>
#include <charconv>
#include <iterator>
#include <list>
#include <utility>
#include "vcd_writer.h"


// -----------------------------
namespace vcd {
namespace utils {
void replace_new_lines(std::string &str, const std::string &sub);
}
using namespace utils;

namespace {
// -----------------------------
// Locale-independent ASCII helpers (std::tolower/isgraph depend on the C locale and are slower)
constexpr char to_lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
constexpr bool is_graph(char c) { return c > ' ' && c < '\x7f'; }

// -----------------------------
// VCD tokens are separated by whitespace, so identifiers must not contain any,
// and must not look like a keyword ("$end", "$var", ...)
bool is_valid_token(const std::string &s)
{
    if (s.empty() || s[0] == '$')
        return false;
    return std::all_of(s.begin(), s.end(), is_graph);
}

// -----------------------------
std::vector<std::string> split(const std::string &s, const std::string &sep)
{
    std::vector<std::string> parts;
    size_t beg = 0;
    while (true)
    {
        const size_t end = s.find(sep, beg);
        if (end == std::string::npos)
        {
            parts.push_back(s.substr(beg));
            break;
        }
        parts.push_back(s.substr(beg, end - beg));
        beg = end + sep.size();
    }
    return parts;
}

// -----------------------------
void make_var_key(std::string &key, const std::string &scope, const std::string &name)
{
    key.clear();
    key += scope;
    key += '\0';
    key += name;
}

std::string var_key(const std::string &scope, const std::string &name)
{
    std::string key;
    key.reserve(scope.size() + name.size() + 1);
    make_var_key(key, scope, name);
    return key;
}

// -----------------------------
// Append the unsigned *value* in the given base (lowercase, same as "{:d}"/"{:x}")
void append_number(std::string &out, uint64_t value, int base)
{
    char digits[64];
    const auto res = std::to_chars(std::begin(digits), std::end(digits), value, base);
    out.append(digits, res.ptr);
}
} // namespace

// -----------------------------
struct Header final
{
    enum KW { KW_TIMESCALE, KW_DATE, KW_COMMENT, KW_VERSION, KW_COUNT_ };
    static constexpr std::array<const char*, KW_COUNT_> kw_names{ "$timescale", "$date", "$comment", "$version" };

    const TimeScale timescale_quan;
    const TimeScaleUnit timescale_unit;
    const std::array<std::string, KW_COUNT_> kw_values;

    Header() = delete;
    Header(Header&&) = default;
    Header(const Header&) = delete;
    Header& operator=(const Header&) = delete;
    Header& operator=(Header&&) = delete;
    ~Header() = default;
    Header(TimeScale     timescale_quan,
              TimeScaleUnit timescale_unit,
              const std::string& date,
              const std::string& comment,
              const std::string& version) :
        timescale_quan{ timescale_quan },
        timescale_unit{ timescale_unit },
        kw_values{ timescale(timescale_quan, timescale_unit),
                   checked_date(date),
                   checked(comment, "comment"),
                   checked(version, "version") }
    {}

private:
    [[nodiscard]] static std::string timescale(TimeScale quan, TimeScaleUnit unit)
    {
        if (quan != TimeScale::one && quan != TimeScale::ten && quan != TimeScale::hundred)
            throw TypeException{ std::format("Invalid time scale quant {}", int(quan)) };
        if (int(unit) < 0 || unit >= TimeScaleUnit::_count_)
            throw TypeException{ std::format("Invalid time scale unit {}", int(unit)) };

        const std::array<const char*, int(TimeScaleUnit::_count_)> TIMESCALE_UNITS{ "s", "ms", "us", "ns", "ps", "fs" };
        return std::to_string(int(quan)) + " " + TIMESCALE_UNITS[int(unit)];
    }
    [[nodiscard]] static const std::string& checked(const std::string &value, const char *what)
    {
        if (value.find("$end") != std::string::npos)
            throw TypeException{ std::format("Header {} must not contain '$end'", what) };
        return value;
    }
    [[nodiscard]] static const std::string& checked_date(const std::string &date)
    {
        if (!utils::validate_date(date))
            throw TypeException{ std::format("Invalid date '{}' format", date) };
        return checked(date, "date");
    }
};

// -----------------------------
HeadPtr make_header(TimeScale timescale_quan, TimeScaleUnit timescale_unit, const std::string &date,
                      const std::string &comment, const std::string &version)
{
    return HeadPtr{ new Header(timescale_quan, timescale_unit, date, comment, version) };
}

// -----------------------------
void HeaderDeleter::operator()(Header *p) { delete p; }

// -----------------------------
struct Scope final
{
    std::string name;
    ScopeType   type;
    std::list<VarPtr> vars;

    Scope(std::string_view name, ScopeType type) :
        name(name), type(type) {}
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
Writer::Writer(std::string filename, HeadPtr &&header, TimeStamp init_timestamp) :
    _timestamp(init_timestamp),
    _header((header) ? std::move(header) : make_header()),
    _scope_sep("."),
    _scope_def_type(ScopeType::module),
    _filename(std::move(filename)),
    _ofile(_filename, std::ios::out | std::ios::trunc | std::ios::binary),
    _dumping(true),
    _registering(true)
{
    if (!_ofile)
        throw Exception{ std::format("Cannot open file '{}'", _filename) };
    // report write errors as exceptions
    _ofile.exceptions(std::ios::badbit | std::ios::failbit);
}

// -----------------------------
Writer::~Writer()
{
    try { close(nullptr); }
    catch (...) {} // destructor must not throw
}

// -----------------------------
VarPtr Writer::register_var(const std::string &scope, const std::string &name, VariableType type,
                               unsigned size, const VarValue &init, bool duplicate_names_check)
{
    if (_closed)
        throw PhaseException{ "Cannot register after close()" };
    if (!_registering)
        throw PhaseException{ std::format("Cannot register new var '{}', registering finished", name) };

    if (scope.empty() || name.empty())
        throw TypeException{ std::format("Empty scope '{}' or name '{}'", scope, name) };
    if (!is_valid_token(name))
        throw TypeException{ std::format("Invalid var name '{}'", name) };
    for (const auto &part : split(scope, _scope_sep))
        if (!is_valid_token(part))
            throw TypeException{ std::format("Invalid scope '{}'", scope) };

    auto key = var_key(scope, name);
    if (duplicate_names_check && _vars.find(key) != _vars.end())
        throw TypeException{ std::format("Duplicate var '{}' in scope '{}'", name, scope) };

    const auto ident = static_cast<unsigned>(_values.size());
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
    VarValue record;
    if (type != VariableType::event)
        pvar->change_record(init_value, record);

    // Only alter state after all checks succeeded
    auto it_scope = _scopes.find(scope);
    if (it_scope == _scopes.end())
        it_scope = _scopes.emplace(scope, std::make_shared<Scope>(scope, _scope_def_type)).first;
    it_scope->second->vars.push_back(pvar);
    _vars.emplace(std::move(key), pvar);
    _values.emplace_back(pvar, std::move(record));
    return pvar;
}

// -----------------------------
void Writer::_advance(TimeStamp timestamp)
{
    if (timestamp < _timestamp)
        throw PhaseException{ std::format("Out of order timestamp {} < {}", timestamp, _timestamp) };
    if (timestamp > _timestamp)
    {
        if (_registering)
            _finalize_registration();
        _timestamp = timestamp;
        _timestamp_printed = false;
    }
}

// -----------------------------
void Writer::_print_timestamp()
{
    if (_timestamp_printed)
        return;
    _buf += '#';
    append_number(_buf, _timestamp, 10);
    _buf += '\n';
    _timestamp_printed = true;
    _write_buf_if_full();
}

// -----------------------------
void Writer::_print_change(const VarValue &value, unsigned ident)
{
    _buf += value;
    append_number(_buf, ident, 16);
    _buf += '\n';
    _write_buf_if_full();
}

// -----------------------------
void Writer::_write_buf()
{
    if (_buf.empty())
        return;
    // clear the buffer even on a write error, it is reported once
    std::string buf;
    buf.swap(_buf);
    _ofile.write(buf.data(), std::streamsize(buf.size()));
    buf.clear();
    _buf.swap(buf); // keep the capacity
}

// -----------------------------
bool Writer::_change(const VarPtr &var, TimeStamp timestamp, const VarValue &value)
{
    if (!var)
        throw TypeException{ "Invalid Variable" };
    if (_closed)
        throw PhaseException{ "Cannot change value after close()" };
    if (timestamp < _timestamp)
        throw PhaseException{ std::format("Out of order value change var '{}'", var->_name) };
    if (var->_ident >= _values.size() || _values[var->_ident].first != var)
        throw TypeException{ std::format("Variable '{}' do not registered", var->_name) };

    // validate the value before any state change
    var->change_record(value, _record);
    _advance(timestamp);

    if (var->_type == VariableType::event)
    {
        // events are not values: every change triggers the event
        if (_registering)
            _finalize_registration();
        if (_dumping)
        {
            _print_timestamp();
            _print_change(_record, var->_ident);
        }
        return true;
    }

    auto &prev = _values[var->_ident].second;
    if (prev == _record)
        return false;
    // the old value's buffer is reused by the next change
    prev.swap(_record);

    // dump it into file
    if (_dumping && !_registering)
    {
        _print_timestamp();
        _print_change(prev, var->_ident);
    }
    return true;
}

// -----------------------------
bool Writer::change(const std::string &scope, const std::string &name, TimeStamp timestamp, const VarValue &value)
{
    return _change(_find_var(scope, name), timestamp, value);
}

// -----------------------------
const VarPtr& Writer::_find_var(const std::string &scope, const std::string &name)
{
    make_var_key(_key, scope, name);
    auto it_var = _vars.find(_key);
    if (it_var == _vars.end())
        throw PhaseException{ std::format("The var '{}' in scope '{}' does not exist", name, scope) };
    return it_var->second;
}

// -----------------------------
VarPtr Writer::var(const std::string &scope, const std::string &name) const
{
    auto it_var = _vars.find(var_key(scope, name));
    if (it_var == _vars.end())
        throw PhaseException{ std::format("The var '{}' in scope '{}' does not exist", name, scope) };
    return it_var->second;
}

// -----------------------------
void Writer::set_scope_type(const std::string &scope, ScopeType scope_type)
{
    auto it = _scopes.find(scope);
    if (it == _scopes.end())
        throw PhaseException{ std::format("Such scope '{}' does not exist", scope) };
    it->second->type = scope_type;
}

// -----------------------------
void Writer::dump_off(TimeStamp timestamp)
{
    if (_closed)
        throw PhaseException{ "Cannot dump_off() after close()" };
    if (timestamp < _timestamp)
        throw PhaseException{ "Out of order dump_off()" };
    if (_registering && timestamp == _timestamp)
    {   // header is not written yet, `_finalize_registration()` handles it
        _dumping = false;
        return;
    }
    _advance(timestamp);
    if (!_dumping)
        return;
    _dump_off();
    _dumping = false;
}

// -----------------------------
void Writer::dump_on(TimeStamp timestamp)
{
    if (_closed)
        throw PhaseException{ "Cannot dump_on() after close()" };
    if (timestamp < _timestamp)
        throw PhaseException{ "Out of order dump_on()" };
    if (_registering && timestamp == _timestamp)
    {   // header is not written yet, `_finalize_registration()` handles it
        _dumping = true;
        return;
    }
    _advance(timestamp);
    if (_dumping)
        return;
    _dumping = true;
    _print_timestamp();
    _dump_values("$dumpon");
}

// -----------------------------
void Writer::flush(const TimeStamp *timestamp)
{
    if (_closed)
        throw PhaseException{ "Cannot flush() after close()" };
    if (timestamp != nullptr && *timestamp > _timestamp)
    {
        _advance(*timestamp);
        if (_dumping)
            _print_timestamp();
    }
    if (_registering)
        _finalize_registration();
    _write_buf();
    _ofile.flush();
}

// -----------------------------
void Writer::close(const TimeStamp *timestamp)
{
    if (_closed)
        return;
    try
    {
        flush(timestamp);
    }
    catch (...)
    {
        _closed = true;
        try { _ofile.close(); } catch (...) {}
        throw;
    }
    _closed = true;
    _ofile.close();
}

// -----------------------------
void Writer::_dump_off()
{
    _print_timestamp();
    _print("$dumpoff\n");
    for (const auto &p : _values)
    {
        const auto ident = p.first->_ident;
        const auto &value = p.second;

        if (value.empty() || value[0] == 'r' || value[0] == 's')
        {} // events, real and string variables cannot have "x" state
        else if (value[0] == 'b')
        { _print("bx {:x}\n", ident); }
        else
        { _print("x{:x}\n", ident); }
    }
    _print("$end\n");
}

// -----------------------------
void Writer::_dump_values(const char *keyword)
{
    _print("{:s}\n", keyword);
    for (const auto &p : _values)
    {
        if (p.second.empty()) // events have no value
            continue;
        _print_change(p.second, p.first->_ident);
    }
    _print("$end\n");
}

// -----------------------------
void Writer::_scope_declaration(const std::string &name, ScopeType type)
{
    const std::array<const char*, 5> SCOPE_TYPES = { "begin", "fork", "function", "module", "task" };
    _print("$scope {:s} {:s} $end\n", SCOPE_TYPES[int(type)], name);
}

// -----------------------------
void Writer::_write_header()
{
    for (int i = 0; i < Header::KW_COUNT_; ++i)
    {
        auto kwname = Header::kw_names[i];
        auto kwvalue = _header->kw_values[i];
        if (kwvalue.empty())
            continue;
        replace_new_lines(kwvalue, "\n\t");
        _print("{:s} {:s} $end\n", kwname, kwvalue);
    }

    // sort scopes by their hierarchical path, so a scope is followed by its children
    std::vector<std::pair<std::vector<std::string>, const Scope*>> scopes;
    scopes.reserve(_scopes.size());
    for (const auto &s : _scopes)
        scopes.emplace_back(split(s.first, _scope_sep), s.second.get());
    std::sort(scopes.begin(), scopes.end(),
              [](const auto &l, const auto &r) { return l.first < r.first; });

    std::vector<std::string> opened;
    for (const auto &[path, scope] : scopes)
    {
        // close the scopes which are not the ancestors of the current one
        size_t common = 0;
        while (common < opened.size() && common < path.size() && opened[common] == path[common])
            ++common;
        while (opened.size() > common)
        {
            _print("$upscope $end\n");
            opened.pop_back();
        }
        // open the (intermediate) scopes
        for (size_t i = opened.size(); i < path.size(); ++i)
        {
            const bool last = (i + 1 == path.size());
            _scope_declaration(path[i], last ? scope->type : _scope_def_type);
            opened.push_back(path[i]);
        }
        // dump variable declartion
        for (const auto& var : scope->vars)
            _print("{:s}\n", var->declartion());
    }
    for (size_t i = 0; i < opened.size(); ++i)
        _print("$upscope $end\n");

    _print("$enddefinitions $end\n");
    // do not need anymore
    _header.reset(nullptr);
}

// -----------------------------
void Writer::_finalize_registration()
{
    assert(_registering);
    _write_header();
    _registering = false;
    if (!_values.empty())
    {
        _print_timestamp();
        _dump_values("$dumpvars");
        if (!_dumping)
            _dump_off();
    }
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

