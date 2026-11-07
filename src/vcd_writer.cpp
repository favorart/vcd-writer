#include <string>
#include <cassert>
#include <algorithm>
#include <array>
#include <iterator>
#include <format>
#include <fstream>
#include <list>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>
#include "vcd_writer.h"
#include "vcd_utils.h"


// -----------------------------
namespace vcd {
using namespace utils;

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
        if (!date.empty() && !utils::validate_date(date))
            throw TypeException{ std::format("Invalid date '{}', expected 'YYYY-MM-DD hh:mm:ss'", date) };
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
struct Scope;
using ScopePtr = std::shared_ptr<Scope>;

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
// State of a writer and its helpers
struct Writer::Impl
{
    Impl(const std::string &filename, HeadPtr head, TimeStamp init_timestamp);

    TimeStamp timestamp;
    HeadPtr header;

    // settings
    std::string scope_sep = ".";
    ScopeType   scope_def_type = ScopeType::module;
    std::ofstream file;

    // scope name -> scope
    std::map<std::string, ScopePtr> scopes;
    // (scope + '\0' + name) -> var
    std::unordered_map<std::string, VarPtr> vars;

    // state
    bool closed = false;
    bool dumping = true;
    bool registering = true;
    bool timestamp_printed = false;

    // registered vars and their last dumped values, indexed by var ident
    std::vector<std::pair<VarPtr, VarValue>> values;

    // reusable buffers: no memory allocation per change in a steady state
    std::string buf;     // formatted output not written to the file yet
    VarValue    record;  // change record of the current change
    std::string key;     // lookup key of change(scope, name)
    static constexpr size_t buf_limit = 64 * 1024;

    //! Validate *ts* and move the current time to it
    void advance(TimeStamp ts);
    //! Write "#<timestamp>" once per timestamp
    void print_timestamp();
    void dump_off();
    void dump_values(const char *keyword);
    void scope_declaration(const std::string& name, ScopeType type);
    //! Dump VCD header into file
    void write_header();
    //! Turn to dumping phase, no more variables regestration allowed
    void finalize_registration();
    //! Format into the output buffer
    template <class... Args>
    void print(std::format_string<Args...> fmt, Args&&... args);
    //! Append "<value><ident in hex>\n" into the output buffer (hot path, no std::format)
    void print_change(const VarValue &value, unsigned ident);
    //! Write the output buffer into the file when it is large enough
    void write_buf_if_full() { if (buf.size() >= buf_limit) write_buf(); }
    void write_buf();
    //! Find a registered var (throws PhaseException), reuses a key buffer
    const VarPtr& find_var(const std::string &scope, const std::string &name);
};

// -----------------------------
Writer::Impl::Impl(const std::string &filename, HeadPtr head, TimeStamp init_timestamp) :
    timestamp(init_timestamp),
    header(head ? std::move(head) : make_header()),
    file(filename, std::ios::out | std::ios::trunc | std::ios::binary)
{
    if (!file)
        throw Exception{ std::format("Cannot open file '{}'", filename) };
    // report write errors as exceptions
    file.exceptions(std::ios::badbit | std::ios::failbit);
}

// -----------------------------
Writer::Writer(std::string filename, HeadPtr &&header, TimeStamp init_timestamp) :
    _impl(std::make_unique<Impl>(filename, std::move(header), init_timestamp))
{}

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
    auto &d = *_impl;
    if (d.closed)
        throw PhaseException{ "Cannot register after close()" };
    if (!d.registering)
        throw PhaseException{ std::format("Cannot register new var '{}', registering finished", name) };

    if (scope.empty() || name.empty())
        throw TypeException{ std::format("Empty scope '{}' or name '{}'", scope, name) };
    if (!is_valid_token(name))
        throw TypeException{ std::format("Invalid var name '{}'", name) };
    for (const auto &part : split(scope, d.scope_sep))
        if (!is_valid_token(part))
            throw TypeException{ std::format("Invalid scope '{}'", scope) };

    std::string key;
    make_var_key(key, scope, name);
    if (duplicate_names_check && d.vars.find(key) != d.vars.end())
        throw TypeException{ std::format("Duplicate var '{}' in scope '{}'", name, scope) };

    const auto ident = static_cast<unsigned>(d.values.size());
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
    auto it_scope = d.scopes.find(scope);
    if (it_scope == d.scopes.end())
        it_scope = d.scopes.emplace(scope, std::make_shared<Scope>(scope, d.scope_def_type)).first;
    it_scope->second->vars.push_back(pvar);
    d.vars.emplace(std::move(key), pvar);
    d.values.emplace_back(pvar, std::move(record));
    return pvar;
}

// -----------------------------
void Writer::Impl::advance(TimeStamp ts)
{
    if (ts < timestamp)
        throw PhaseException{ std::format("Out of order timestamp {} < {}", ts, timestamp) };
    if (ts > timestamp)
    {
        if (registering)
            finalize_registration();
        timestamp = ts;
        timestamp_printed = false;
    }
}

// -----------------------------
void Writer::Impl::print_timestamp()
{
    if (timestamp_printed)
        return;
    buf += '#';
    append_number(buf, timestamp, 10);
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
    append_number(buf, ident, 16);
    buf += '\n';
    write_buf_if_full();
}

// -----------------------------
void Writer::Impl::write_buf()
{
    if (buf.empty())
        return;
    // clear the buffer even on a write error, it is reported once
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
    if (d.closed)
        throw PhaseException{ "Cannot change value after close()" };
    if (timestamp < d.timestamp)
        throw PhaseException{ std::format("Out of order value change var '{}'", var->_name) };
    if (var->_ident >= d.values.size() || d.values[var->_ident].first != var)
        throw TypeException{ std::format("Variable '{}' do not registered", var->_name) };

    // validate the value before any state change
    var->change_record(value, d.record);
    d.advance(timestamp);

    if (var->_type == VariableType::event)
    {
        // events are not values: every change triggers the event
        if (d.registering)
            d.finalize_registration();
        if (d.dumping)
        {
            d.print_timestamp();
            d.print_change(d.record, var->_ident);
        }
        return true;
    }

    auto &prev = d.values[var->_ident].second;
    if (prev == d.record)
        return false;
    // the old value's buffer is reused by the next change
    prev.swap(d.record);

    // dump it into file
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
    return change(_impl->find_var(scope, name), timestamp, value);
}

// -----------------------------
const VarPtr& Writer::Impl::find_var(const std::string &scope, const std::string &name)
{
    make_var_key(key, scope, name);
    auto it_var = vars.find(key);
    if (it_var == vars.end())
        throw PhaseException{ std::format("The var '{}' in scope '{}' does not exist", name, scope) };
    return it_var->second;
}

// -----------------------------
VarPtr Writer::var(const std::string &scope, const std::string &name) const
{
    std::string key;
    make_var_key(key, scope, name);
    auto it_var = _impl->vars.find(key);
    if (it_var == _impl->vars.end())
        throw PhaseException{ std::format("The var '{}' in scope '{}' does not exist", name, scope) };
    return it_var->second;
}

// -----------------------------
void Writer::set_scope_type(const std::string &scope, ScopeType scope_type)
{
    auto it = _impl->scopes.find(scope);
    if (it == _impl->scopes.end())
        throw PhaseException{ std::format("Such scope '{}' does not exist", scope) };
    it->second->type = scope_type;
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
void Writer::dump_off(TimeStamp timestamp)
{
    auto &d = *_impl;
    if (d.closed)
        throw PhaseException{ "Cannot dump_off() after close()" };
    if (timestamp < d.timestamp)
        throw PhaseException{ "Out of order dump_off()" };
    if (d.registering && timestamp == d.timestamp)
    {   // header is not written yet, `finalize_registration()` handles it
        d.dumping = false;
        return;
    }
    d.advance(timestamp);
    if (!d.dumping)
        return;
    d.dump_off();
    d.dumping = false;
}

// -----------------------------
void Writer::dump_on(TimeStamp timestamp)
{
    auto &d = *_impl;
    if (d.closed)
        throw PhaseException{ "Cannot dump_on() after close()" };
    if (timestamp < d.timestamp)
        throw PhaseException{ "Out of order dump_on()" };
    if (d.registering && timestamp == d.timestamp)
    {   // header is not written yet, `finalize_registration()` handles it
        d.dumping = true;
        return;
    }
    d.advance(timestamp);
    if (d.dumping)
        return;
    d.dumping = true;
    d.print_timestamp();
    d.dump_values("$dumpon");
}

// -----------------------------
void Writer::flush(const TimeStamp *timestamp)
{
    auto &d = *_impl;
    if (d.closed)
        throw PhaseException{ "Cannot flush() after close()" };
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
void Writer::Impl::dump_off()
{
    print_timestamp();
    print("$dumpoff\n");
    for (const auto &p : values)
    {
        const auto ident = p.first->_ident;
        const auto &value = p.second;

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
    print("{:s}\n", keyword);
    for (const auto &p : values)
    {
        if (p.second.empty()) // events have no value
            continue;
        print_change(p.second, p.first->_ident);
    }
    print("$end\n");
}

// -----------------------------
void Writer::Impl::scope_declaration(const std::string &name, ScopeType type)
{
    const std::array<const char*, 5> SCOPE_TYPES = { "begin", "fork", "function", "module", "task" };
    print("$scope {:s} {:s} $end\n", SCOPE_TYPES[int(type)], name);
}

// -----------------------------
void Writer::Impl::write_header()
{
    for (int i = 0; i < Header::KW_COUNT_; ++i)
    {
        const auto &kwvalue = header->kw_values[i];
        if (kwvalue.empty())
            continue;
        print("{:s} {:s} $end\n", Header::kw_names[i], indent_lines(kwvalue));
    }

    // sort scopes by their hierarchical path, so a scope is followed by its children
    std::vector<std::pair<std::vector<std::string>, const Scope*>> sorted;
    sorted.reserve(scopes.size());
    for (const auto &s : scopes)
        sorted.emplace_back(split(s.first, scope_sep), s.second.get());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto &l, const auto &r) { return l.first < r.first; });

    std::vector<std::string> opened;
    for (const auto &[path, scope] : sorted)
    {
        // close the scopes which are not the ancestors of the current one
        size_t common = 0;
        while (common < opened.size() && common < path.size() && opened[common] == path[common])
            ++common;
        while (opened.size() > common)
        {
            print("$upscope $end\n");
            opened.pop_back();
        }
        // open the (intermediate) scopes
        for (size_t i = opened.size(); i < path.size(); ++i)
        {
            const bool last = (i + 1 == path.size());
            scope_declaration(path[i], last ? scope->type : scope_def_type);
            opened.push_back(path[i]);
        }
        // dump variable declartion
        for (const auto& var : scope->vars)
            print("{:s}\n", var->declartion());
    }
    for (size_t i = 0; i < opened.size(); ++i)
        print("$upscope $end\n");

    print("$enddefinitions $end\n");
    // do not need anymore
    header.reset(nullptr);
}

// -----------------------------
void Writer::Impl::finalize_registration()
{
    assert(registering);
    write_header();
    registering = false;
    if (!values.empty())
    {
        print_timestamp();
        dump_values("$dumpvars");
        if (!dumping)
            dump_off();
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

