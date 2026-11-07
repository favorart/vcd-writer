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
#include "vcd_variable.h"

namespace vcd {
using namespace utils;

namespace {
constexpr std::array<const char*, size_t(ScopeType::_count_)> SCOPE_TYPES{ "begin", "fork", "function", "module", "task" };
constexpr int TIMESTAMP_BASE = 10;  // "#<timestamp>" is decimal
constexpr int IDENT_BASE = 16;      // identifiers are hexadecimal, the same as in "$var"
}

// -----------------------------
// State of a writer and its helpers
struct Writer::Impl
{
    struct Scope
    {
        ScopeType type;
        std::vector<const Variable*> vars;
    };

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
const VarPtr& Writer::Impl::find_var(const std::string &scope, const std::string &name, std::string &var_key) const
{
    make_var_key(var_key, scope, name);
    const auto it = vars.find(var_key);
    if (it == vars.end())
        throw PhaseException{ std::format("The var '{}' in scope '{}' does not exist", name, scope) };
    return it->second;
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
void Writer::Impl::dump_values(const char *keyword)
{
    print("{}\n", keyword);
    for (const auto &var : idents)
        if (var->kind != Variable::Kind::event)
            print_change(values[var->ident], var->ident);
    print("$end\n");
}

// -----------------------------
void Writer::Impl::dump_off_values()
{
    print("$dumpoff\n");
    for (const auto &var : idents)
    {
        if (var->kind == Variable::Kind::scalar)
            print("x{:x}\n", var->ident);
        else if (var->kind == Variable::Kind::vector)
            print("bx {:x}\n", var->ident);
    }
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
            print("{}\n", var->declaration());
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

    auto var = std::make_shared<Variable>(name, type, size, unsigned(d.idents.size()));
    // events have no value, other vars validate the initial value ("x" of a real is 0)
    VarValue init_record;
    if (var->kind == Variable::Kind::real && init == VarValue{ Bit::undef })
        var->change_record("0.0", init_record);
    else if (var->kind != Variable::Kind::event)
        var->change_record(init, init_record);

    // only alter the state after all checks succeeded
    d.scopes.try_emplace(scope, Impl::Scope{ d.scope_def_type, {} }).first->second.vars.push_back(var.get());
    d.vars.emplace(std::move(var_key), var);
    d.idents.push_back(var);
    d.values.push_back(std::move(init_record));
    return var;
}

// -----------------------------
bool Writer::change(const VarPtr &var, TimeStamp timestamp, const VarValue &value)
{
    auto &d = *_impl;
    if (!var)
        throw TypeException{ "Invalid Variable" };
    d.check_open("change a value");
    if (timestamp < d.timestamp)
        throw PhaseException{ std::format("Out of order value change var '{}'", var->name) };
    if (var->ident >= d.idents.size() || d.idents[var->ident] != var)
        throw TypeException{ std::format("Variable '{}' do not registered", var->name) };

    var->change_record(value, d.record); // validate before any state change
    d.advance(timestamp);

    if (var->kind == Variable::Kind::event)
    {   // events are not values: every change triggers the event
        if (d.registering)
            d.finalize_registration();
        if (d.dumping)
        {
            d.print_timestamp();
            d.print_change(d.record, var->ident);
        }
        return true;
    }

    auto &prev = d.values[var->ident];
    if (prev == d.record)
        return false;
    prev.swap(d.record); // the old value's buffer is reused by the next change

    // during registration the value goes to "$dumpvars"
    if (d.dumping && !d.registering)
    {
        d.print_timestamp();
        d.print_change(prev, var->ident);
    }
    return true;
}

// -----------------------------
bool Writer::change(const std::string &scope, const std::string &name, TimeStamp timestamp, const VarValue &value)
{
    return change(_impl->find_var(scope, name, _impl->key), timestamp, value);
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
VarPtr Writer::var(const std::string &scope, const std::string &name) const
{
    std::string var_key;
    return _impl->find_var(scope, name, var_key);
}

}
