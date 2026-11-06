#include <string>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <array>
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
// VCD tokens are separated by whitespace, so identifiers must not contain any,
// and must not look like a keyword ("$end", "$var", ...)
bool is_valid_token(const std::string &s)
{
    if (s.empty() || s[0] == '$')
        return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return std::isgraph(static_cast<unsigned char>(c)) != 0; });
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
std::string var_key(const std::string &scope, const std::string &name)
{
    std::string key;
    key.reserve(scope.size() + name.size() + 1);
    key += scope;
    key += '\0';
    key += name;
    return key;
}
} // namespace

// -----------------------------
struct VCDHeader final
{
    enum KW { KW_TIMESCALE, KW_DATE, KW_COMMENT, KW_VERSION, KW_COUNT_ };
    static constexpr std::array<const char*, KW_COUNT_> kw_names{ "$timescale", "$date", "$comment", "$version" };

    const TimeScale timescale_quan;
    const TimeScaleUnit timescale_unit;
    const std::array<std::string, KW_COUNT_> kw_values;

    VCDHeader() = delete;
    VCDHeader(VCDHeader&&) = default;
    VCDHeader(const VCDHeader&) = delete;
    VCDHeader& operator=(const VCDHeader&) = delete;
    VCDHeader& operator=(VCDHeader&&) = delete;
    ~VCDHeader() = default;
    VCDHeader(TimeScale     timescale_quan,
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
        if (quan != TimeScale::ONE && quan != TimeScale::TEN && quan != TimeScale::HUNDRED)
            throw VCDTypeException{ std::format("Invalid time scale quant {}", int(quan)) };
        if (int(unit) < 0 || unit >= TimeScaleUnit::_count_)
            throw VCDTypeException{ std::format("Invalid time scale unit {}", int(unit)) };

        const std::array<const char*, int(TimeScaleUnit::_count_)> TIMESCALE_UNITS{ "s", "ms", "us", "ns", "ps", "fs" };
        return std::to_string(int(quan)) + " " + TIMESCALE_UNITS[int(unit)];
    }
    [[nodiscard]] static const std::string& checked(const std::string &value, const char *what)
    {
        if (value.find("$end") != std::string::npos)
            throw VCDTypeException{ std::format("Header {} must not contain '$end'", what) };
        return value;
    }
    [[nodiscard]] static const std::string& checked_date(const std::string &date)
    {
        if (!utils::validate_date(date))
            throw VCDTypeException{ std::format("Invalid date '{}' format", date) };
        return checked(date, "date");
    }
};

// -----------------------------
HeadPtr makeVCDHeader(TimeScale timescale_quan, TimeScaleUnit timescale_unit, const std::string &date,
                      const std::string &comment, const std::string &version)
{
    return HeadPtr{ new VCDHeader(timescale_quan, timescale_unit, date, comment, version) };
}

// -----------------------------
void VCDHeaderDeleter::operator()(VCDHeader *p) { delete p; }

// -----------------------------
struct VCDScope final
{
    std::string name;
    ScopeType   type;
    std::list<VarPtr> vars;

    VCDScope(std::string_view name, ScopeType type) :
        name(name), type(type) {}
};

// -----------------------------
// VCD variable details needed to call :meth:`VCDWriter.change()`.
class VCDVariable
{
public:
    VCDVariable() = delete;
    VCDVariable(VCDVariable&&) = delete;
    VCDVariable& operator=(VCDVariable&&) = delete;
    VCDVariable(const VCDVariable&) = delete;
    VCDVariable& operator=(const VCDVariable&) = delete;

protected:
    VCDVariable(std::string name, VariableType type, unsigned size, unsigned ident) :
        _ident(ident), _type(type), _name(std::move(name)), _size(size) {}

    unsigned     _ident;  // internal ID used in VCD output stream
    VariableType _type;   // VCD variable type, one of `VariableTypes`
    std::string  _name;   // human-readable name
    unsigned     _size;   // size of variable, in bits

    //! string representation of variable types
    static const std::array<const char*, 19> VAR_TYPES;
    static_assert(int(VariableType::wor) + 1 == 19, "VAR_TYPES must match VariableType");

public:
    virtual ~VCDVariable() = default;

    //! string representation of variable declartion in VCD
    [[nodiscard]] std::string declartion() const;
    //! string representation of value change record in VCD
    [[nodiscard]] virtual VarValue change_record(const VarValue &value) const = 0;

    friend class VCDWriter;
};

// -----------------------------
const std::array<const char*, 19> VCDVariable::VAR_TYPES = {
    "wire", "reg", "string", "parameter", "integer", "real", "realtime", "time", "event",
    "supply0", "supply1", "tri", "triand", "trior", "trireg", "tri0", "tri1", "wand", "wor"
};

// -----------------------------
// One-bit VCD scalar is a 4-state variable and thus may have one of
// `VCDValues`. An empty *value* is the same as `VCDValues::UNDEF`
struct VCDScalarVariable : public VCDVariable
{
    VCDScalarVariable(const std::string &name, VariableType type, unsigned size, unsigned ident) :
        VCDVariable(name, type, size, ident) {}
    [[nodiscard]] VarValue change_record(const VarValue &value) const override
    {
        if (value.empty())
            return { char(VCDValues::UNDEF) };
        const char c = char(std::tolower(static_cast<unsigned char>(value[0])));
        if (value.size() != 1 || (c != VCDValues::ONE   && c != VCDValues::ZERO
                               && c != VCDValues::UNDEF && c != VCDValues::HIGHV))
            throw VCDTypeException{ std::format("Invalid scalar value '{}'", value) };
        return {c};
    }
};

// -----------------------------
// String variable as known by GTKWave. Any `string` (character-chain)
// can be displayed as a change.This type is only supported by GTKWave.
// The value must be non-empty and must not contain whitespace or control characters.
struct VCDStringVariable : public VCDVariable
{
    VCDStringVariable(const std::string &name, VariableType type, unsigned size, unsigned ident) :
        VCDVariable(name, type, size, ident) {}
    [[nodiscard]]VarValue change_record(const VarValue &value) const override
    {
        const bool ok = !value.empty() && std::all_of(value.begin(), value.end(),
            [](char c) { return std::isgraph(static_cast<unsigned char>(c)) != 0; });
        if (!ok)
            throw VCDTypeException{ std::format("Invalid string value '{}'", value) };
        return "s" + value + " ";
    }
};

// -----------------------------
// Real (IEEE-754 double-precision floating point) variable. Values must
// be numeric and can't be `VCDValues::UNDEF` or `VCDValues::HIGHV` states
struct VCDRealVariable : public VCDVariable
{
    VCDRealVariable(const std::string &name, VariableType type, unsigned size, unsigned ident) :
        VCDVariable(name, type, size, ident) {}
    [[nodiscard]]std::string change_record(const VarValue &value) const override
    {
        double d = 0.0;
        size_t idx = 0;
        try { d = std::stod(value, &idx); }
        catch (const std::exception&) { idx = 0; }
        if (idx == 0 || idx != value.size())
            throw VCDTypeException{ std::format("Invalid real value '{}'", value) };
        return std::format("r{:.16g} ", d);
    }
};

// -----------------------------
// Bit vector variable type for the various non-scalar and non-real
// variable types, including integer, register, wire, etc.
struct VCDVectorVariable : public VCDVariable
{
    VCDVectorVariable(const std::string &name, VariableType type, unsigned size, unsigned ident) :
        VCDVariable(name, type, size, ident) {}
    [[nodiscard]]std::string change_record(const VarValue &value) const override;
};

// -----------------------------
VCDWriter::VCDWriter(std::string filename, HeadPtr &&header, TimeStamp init_timestamp) :
    _timestamp(init_timestamp),
    _header((header) ? std::move(header) : makeVCDHeader()),
    _scope_sep("."),
    _scope_def_type(ScopeType::module),
    _filename(std::move(filename)),
    _ofile(_filename, std::ios::out | std::ios::trunc | std::ios::binary),
    _dumping(true),
    _registering(true)
{
    if (!_ofile)
        throw VCDException{ std::format("Cannot open file '{}'", _filename) };
    // report write errors as exceptions
    _ofile.exceptions(std::ios::badbit | std::ios::failbit);
}

// -----------------------------
VCDWriter::~VCDWriter()
{
    try { close(nullptr); }
    catch (...) {} // destructor must not throw
}

// -----------------------------
VarPtr VCDWriter::register_var(const std::string &scope, const std::string &name, VariableType type,
                               unsigned size, const VarValue &init, bool duplicate_names_check)
{
    if (_closed)
        throw VCDPhaseException{ "Cannot register after close()" };
    if (!_registering)
        throw VCDPhaseException{ std::format("Cannot register new var '{}', registering finished", name) };

    if (scope.empty() || name.empty())
        throw VCDTypeException{ std::format("Empty scope '{}' or name '{}'", scope, name) };
    if (!is_valid_token(name))
        throw VCDTypeException{ std::format("Invalid var name '{}'", name) };
    for (const auto &part : split(scope, _scope_sep))
        if (!is_valid_token(part))
            throw VCDTypeException{ std::format("Invalid scope '{}'", scope) };

    auto key = var_key(scope, name);
    if (duplicate_names_check && _vars.find(key) != _vars.end())
        throw VCDTypeException{ std::format("Duplicate var '{}' in scope '{}'", name, scope) };

    const auto ident = static_cast<unsigned>(_values.size());
    auto sz = [&size](unsigned def) { return (size ? size : def);  };

    VarPtr pvar;
    VarValue init_value(init);
    switch (type)
    {
        case VariableType::integer:
        case VariableType::time:
            if (sz(64) == 1)
                pvar = VarPtr(new VCDScalarVariable(name, type, 1, ident));
            else
                pvar = VarPtr(new VCDVectorVariable(name, type, sz(64), ident));
            break;

        case VariableType::real:
        case VariableType::realtime:
            pvar = VarPtr(new VCDRealVariable(name, type, sz(64), ident));
            if (init_value.size() == 1 && init_value[0] == VCDValues::UNDEF)
                init_value = "0.0";
            break;

        case VariableType::string:
            pvar = VarPtr(new VCDStringVariable(name, type, sz(1), ident));
            break;

        case VariableType::event:
            pvar = VarPtr(new VCDScalarVariable(name, type, 1, ident));
            break;

        default:
            if (!size)
                throw VCDTypeException{ std::format("Must supply size for type '{}' of var '{}'",
                                               VCDVariable::VAR_TYPES[int(type)], name) };
            pvar = VarPtr(new VCDVectorVariable(name, type, size, ident));
            break;
    }
    // events have no value, other vars validate the initial value
    VarValue record = (type == VariableType::event) ? VarValue{} : pvar->change_record(init_value);

    // Only alter state after all checks succeeded
    auto it_scope = _scopes.find(scope);
    if (it_scope == _scopes.end())
        it_scope = _scopes.emplace(scope, std::make_shared<VCDScope>(scope, _scope_def_type)).first;
    it_scope->second->vars.push_back(pvar);
    _vars.emplace(std::move(key), pvar);
    _values.emplace_back(pvar, std::move(record));
    return pvar;
}

// -----------------------------
void VCDWriter::_advance(TimeStamp timestamp)
{
    if (timestamp < _timestamp)
        throw VCDPhaseException{ std::format("Out of order timestamp {} < {}", timestamp, _timestamp) };
    if (timestamp > _timestamp)
    {
        if (_registering)
            _finalize_registration();
        _timestamp = timestamp;
        _timestamp_printed = false;
    }
}

// -----------------------------
void VCDWriter::_print_timestamp()
{
    if (_timestamp_printed)
        return;
    _print("#{:d}\n", _timestamp);
    _timestamp_printed = true;
}

// -----------------------------
bool VCDWriter::_change(const VarPtr &var, TimeStamp timestamp, const VarValue &value)
{
    if (!var)
        throw VCDTypeException{ "Invalid VCDVariable" };
    if (_closed)
        throw VCDPhaseException{ "Cannot change value after close()" };
    if (timestamp < _timestamp)
        throw VCDPhaseException{ std::format("Out of order value change var '{}'", var->_name) };
    if (var->_ident >= _values.size() || _values[var->_ident].first != var)
        throw VCDTypeException{ std::format("VCDVariable '{}' do not registered", var->_name) };

    // validate the value before any state change
    VarValue change_value = var->change_record(value);
    _advance(timestamp);

    if (var->_type == VariableType::event)
    {
        // events are not values: every change triggers the event
        if (_registering)
            _finalize_registration();
        if (_dumping)
        {
            _print_timestamp();
            _print("{:s}{:x}\n", change_value, var->_ident);
        }
        return true;
    }

    auto &prev = _values[var->_ident].second;
    if (prev == change_value)
        return false;
    prev = std::move(change_value);

    // dump it into file
    if (_dumping && !_registering)
    {
        _print_timestamp();
        _print("{:s}{:x}\n", prev, var->_ident);
    }
    return true;
}

// -----------------------------
bool VCDWriter::change(const std::string &scope, const std::string &name, TimeStamp timestamp, const VarValue &value)
{
    return _change(var(scope, name), timestamp, value);
}

// -----------------------------
VarPtr VCDWriter::var(const std::string &scope, const std::string &name) const
{
    auto it_var = _vars.find(var_key(scope, name));
    if (it_var == _vars.end())
        throw VCDPhaseException{ std::format("The var '{}' in scope '{}' does not exist", name, scope) };
    return it_var->second;
}

// -----------------------------
void VCDWriter::set_scope_type(const std::string &scope, ScopeType scope_type)
{
    auto it = _scopes.find(scope);
    if (it == _scopes.end())
        throw VCDPhaseException{ std::format("Such scope '{}' does not exist", scope) };
    it->second->type = scope_type;
}

// -----------------------------
void VCDWriter::dump_off(TimeStamp timestamp)
{
    if (_closed)
        throw VCDPhaseException{ "Cannot dump_off() after close()" };
    if (timestamp < _timestamp)
        throw VCDPhaseException{ "Out of order dump_off()" };
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
void VCDWriter::dump_on(TimeStamp timestamp)
{
    if (_closed)
        throw VCDPhaseException{ "Cannot dump_on() after close()" };
    if (timestamp < _timestamp)
        throw VCDPhaseException{ "Out of order dump_on()" };
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
void VCDWriter::flush(const TimeStamp *timestamp)
{
    if (_closed)
        throw VCDPhaseException{ "Cannot flush() after close()" };
    if (timestamp != nullptr && *timestamp > _timestamp)
    {
        _advance(*timestamp);
        if (_dumping)
            _print_timestamp();
    }
    if (_registering)
        _finalize_registration();
    _ofile.flush();
}

// -----------------------------
void VCDWriter::close(const TimeStamp *timestamp)
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
void VCDWriter::_dump_off()
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
void VCDWriter::_dump_values(const char *keyword)
{
    _print("{:s}\n", keyword);
    for (const auto &p : _values)
    {
        if (p.second.empty()) // events have no value
            continue;
        _print("{:s}{:x}\n", p.second, p.first->_ident);
    }
    _print("$end\n");
}

// -----------------------------
void VCDWriter::_scope_declaration(const std::string &name, ScopeType type)
{
    const std::array<const char*, 5> SCOPE_TYPES = { "begin", "fork", "function", "module", "task" };
    _print("$scope {:s} {:s} $end\n", SCOPE_TYPES[int(type)], name);
}

// -----------------------------
void VCDWriter::_write_header()
{
    for (int i = 0; i < VCDHeader::KW_COUNT_; ++i)
    {
        auto kwname = VCDHeader::kw_names[i];
        auto kwvalue = _header->kw_values[i];
        if (kwvalue.empty())
            continue;
        replace_new_lines(kwvalue, "\n\t");
        _print("{:s} {:s} $end\n", kwname, kwvalue);
    }

    // sort scopes by their hierarchical path, so a scope is followed by its children
    std::vector<std::pair<std::vector<std::string>, const VCDScope*>> scopes;
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
void VCDWriter::_finalize_registration()
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
std::string VCDVariable::declartion() const
{
    return std::format("$var {} {} {:x} {} $end", VAR_TYPES[int(_type)], _size, _ident, _name);
}

// -----------------------------
//  :Warning: *value* is string where all characters must be one of `VCDValues`.
//  An empty  *value* is the same as `VCDValues::UNDEF`.
//  A shorter *value* is left-extended: with 'x'/'z' if its leftmost bit is 'x'/'z',
//  with '0' otherwise (IEEE 1364-2005, 18.2.1).
VarValue VCDVectorVariable::change_record(const VarValue &value) const
{
    if (value.size() > _size)
        throw VCDTypeException{ std::format("Invalid binary vector value '{}' size '{}'", value, _size) };

    VarValue val;
    val.reserve(_size + 2);
    val += 'b';
    if (value.empty())
        val.append(_size, VCDValues::UNDEF);
    else
    {
        const char first = char(std::tolower(static_cast<unsigned char>(value[0])));
        const char fill = (first == VCDValues::UNDEF || first == VCDValues::HIGHV) ? first : char(VCDValues::ZERO);
        val.append(_size - value.size(), fill);
        for (const char ch : value)
        {
            const char c = char(std::tolower(static_cast<unsigned char>(ch)));
            if (c != VCDValues::ONE && c != VCDValues::ZERO && c != VCDValues::UNDEF && c != VCDValues::HIGHV)
                throw VCDTypeException{ std::format("Invalid binary vector value '{}' size '{}'", value, _size) };
            val += c;
        }
    }
    val += ' ';
    return val;
}

// -----------------------------
} //end namespace vcd

