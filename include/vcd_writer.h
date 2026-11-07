#pragma once

#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <unordered_map>
#include <string>
#include <cctype>
#include <memory>
#include <utility>
#include <vector>

namespace vcd {
namespace utils {
// -----------------------------
std::string now();
bool validate_date(const std::string&);
}

// -----------------------------
// valid VCD scope types
enum class ScopeType : char
{ begin, fork, function, module, task };
// valid VCD variable types
enum class VariableType : char
{ wire, reg, string, parameter, integer, real, realtime, time, event,
  supply0, supply1, tri, triand, trior, trireg, tri0, tri1, wand, wor };
// valid timescale numbers
enum class TimeScale : char
{ one = 1, ten = 10, hundred = 100, _count_ };
// valid timescale units
enum class TimeScaleUnit : char
{ s, ms, us, ns, ps, fs, _count_ };
// -----------------------------
enum Bit : char
{ one='1', zero='0', undef='x', highz='z', _count_ };

using TimeStamp = uint64_t;
using VarValue = std::string;

// -----------------------------
class Exception : public std::exception
{
public:
    explicit Exception(const std::string & message) : m_message("VCD error: " + message) {}
    [[nodiscard]]  const char* what() const noexcept override { return m_message.c_str(); }

private:
    const std::string m_message;
};

class PhaseException : public Exception
{
public:
    explicit PhaseException(const std::string& message) : Exception(message) {}
};

class TypeException : public Exception
{
public:
    explicit TypeException(const std::string& message) : Exception(message) {}
};

// -----------------------------
struct Scope;
using ScopePtr = std::shared_ptr<Scope>;

// -----------------------------
class Variable;
using VarPtr = std::shared_ptr<Variable>;

// -----------------------------
struct Header;
struct HeaderDeleter { void operator()(Header *p); };
using HeadPtr = std::unique_ptr<Header, HeaderDeleter>;

// -----------------------------
// Header values (date, comment, version) must not contain the "$end" keyword.
HeadPtr make_header(TimeScale     timescale_quan = TimeScale::one,
                      TimeScaleUnit timescale_unit = TimeScaleUnit::ns,
                      const std::string& date = utils::now(),
                      const std::string& comment = "",
                      const std::string& version = "");

// -----------------------------
// Writer of a Value Change Dump file
// A VCD file captures time-ordered changes to the value of variables
class Writer
{
public:
    // Takes ownership of *header* (a null header is replaced by the default one)
    Writer(std::string filename, HeadPtr &&header, TimeStamp init_timestamp = 0u);
    Writer(std::string filename, HeadPtr &header, TimeStamp init_timestamp = 0u)
        : Writer(std::move(filename), std::move(header), init_timestamp) {}
    Writer(Writer&&) = delete;
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    Writer& operator=(Writer&&) = delete;

    // Never throws: errors on the implicit close are swallowed.
    // Call `close()` explicitly to observe them.
    virtual ~Writer();

    // Register a VCD variable and return its mark to change value further.
    // Remember, all VCD variables must be registered prior to any value changes.
    // Note, *size* may be `0`, some types ("int", "real", "event") have a default size.
    // Scope and name must consist of printable non-whitespace characters only.
    VarPtr register_var(const std::string &scope,                  // Variable belongs within the hierarchical scope
                        const std::string &name,                   // Human-readable variable idetifier
                        VariableType type = var_def_type,          // Verilog data type of variable
                        unsigned size = 0,                         // Size of variable, in bits
                        const VarValue &init = {Bit::undef}, // Initial value (optional)
                        bool duplicate_names_check = true);        // speed-up (optimisation)

    // Change variable's value in VCD stream.
    // Call this method, for all variables changed on this *timestamp*.
    // It is okay to call it multiple times with the same *timestamp*,
    // but never call with a past *timestamp*
    // Return:  *true* if new_value is dumped into VCD file,
    //         *false* if new_value is not changed from priveios *timestamp* for a given var
    // Events are never deduplicated: every call triggers the event.
    bool change(const VarPtr &var, TimeStamp timestamp, const VarValue &value)
    { return _change(var, timestamp, value); }

    bool change(const std::string &scope, const std::string &name, TimeStamp timestamp, const VarValue &value);

    // Suspend dumping to VCD file
    void dump_off(TimeStamp timestamp);
    // Resume dumping to VCD file
    void dump_on(TimeStamp timestamp);

    // Flush any buffered VCD data to output file.
    // If the VCD header has not already been written, calling `flush()` will force
    // the header to be written thus disallowing any further variable registrations.
    // If *timestamp* is given, it is written to the file (if later than the current one).
    void flush(const TimeStamp *timestamp = nullptr);
    // Close VCD writer. Any buffered VCD data is flushed to the output file
    // and the output file is closed.
    // After `close()`, NO variable registration or value changes will be accepted.
    void close(const TimeStamp *timestamp = nullptr);

    //! VCD viewer applications may display different scope types differently
    void set_scope_type(const std::string& scope, ScopeType);

    void set_scope_default_type(ScopeType type)
    { _scope_def_type = type; }

    void set_scope_sep(const std::string& scope_sep)
    {
        if (scope_sep.empty() || scope_sep == _scope_sep)
            return;
        _scope_sep = scope_sep;
    }
    //! get VCD Variable (throws PhaseException if it is not registered)
    VarPtr var(const std::string &scope, const std::string &name) const;

    static const VariableType var_def_type = VariableType::integer;

protected:
    bool _change(const VarPtr&, TimeStamp, const VarValue&);
    //! Validate *timestamp* and move the current time to it
    void _advance(TimeStamp timestamp);
    //! Write "#<timestamp>" once per timestamp
    void _print_timestamp();
    void _dump_off();
    void _dump_values(const char *keyword);
    void _scope_declaration(const std::string& name, ScopeType type);
    //! Dump VCD header into file
    void _write_header();
    //! Turn to dumping phase, no more variables regestration allowed
    void _finalize_registration();
    //! Format into the output buffer
    template <class... Args>
    void _print(std::format_string<Args...> fmt, Args&&... args)
    {
        std::format_to(std::back_inserter(_buf), fmt, std::forward<Args>(args)...);
        _write_buf_if_full();
    }
    //! Append "<value><ident in hex>\n" into the output buffer (hot path, no std::format)
    void _print_change(const VarValue &value, unsigned ident);
    //! Write the output buffer into the file when it is large enough
    void _write_buf_if_full()
    {
        if (_buf.size() >= buf_limit)
            _write_buf();
    }
    void _write_buf();
    //! Find a registered var (throws PhaseException), reuses a key buffer
    const VarPtr& _find_var(const std::string &scope, const std::string &name);

    static constexpr size_t buf_limit = 64 * 1024;

private:
    TimeStamp _timestamp;
    HeadPtr _header;

    // settings
    std::string _scope_sep;
    ScopeType   _scope_def_type{};
    std::string _filename;
    std::ofstream _ofile;

    // scope name -> scope
    std::map<std::string, ScopePtr> _scopes;
    // (scope + '\0' + name) -> var
    std::unordered_map<std::string, VarPtr> _vars;

    // state
    bool _closed{};
    bool _dumping{};
    bool _registering{};
    bool _timestamp_printed{};

    // registered vars and their last dumped values, indexed by var ident
    std::vector<std::pair<VarPtr, VarValue>> _values;

    // reusable buffers: no memory allocation per change in a steady state
    std::string _buf;     // formatted output not written to the file yet
    VarValue    _record;  // change record of the current change
    std::string _key;     // lookup key of change(scope, name)
};

// -----------------------------
using WriterPtr = std::shared_ptr<Writer>;

}

