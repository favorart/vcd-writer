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
{ ONE = 1, TEN = 10, HUNDRED = 100, _COUNT_ };
// valid timescale units
enum class TimeScaleUnit : char
{ s, ms, us, ns, ps, fs, _count_ };
// -----------------------------
enum VCDValues : char
{ ONE='1', ZERO='0', UNDEF='x', HIGHV='z', _COUNT_ };

using TimeStamp = uint64_t;
using VarValue = std::string;

// -----------------------------
class VCDException : public std::exception
{
public:
    explicit VCDException(const std::string & message) : m_message("VCD error: " + message) {}
    [[nodiscard]]  const char* what() const noexcept override { return m_message.c_str(); }

private:
    const std::string m_message;
};

class VCDPhaseException : public VCDException
{
public:
    explicit VCDPhaseException(const std::string& message) : VCDException(message) {}
};

class VCDTypeException : public VCDException
{
public:
    explicit VCDTypeException(const std::string& message) : VCDException(message) {}
};

// -----------------------------
struct VCDScope;
using ScopePtr = std::shared_ptr<VCDScope>;

// -----------------------------
class VCDVariable;
using VarPtr = std::shared_ptr<VCDVariable>;

// -----------------------------
struct VCDHeader;
struct VCDHeaderDeleter { void operator()(VCDHeader *p); };
using HeadPtr = std::unique_ptr<VCDHeader, VCDHeaderDeleter>;

// -----------------------------
// Header values (date, comment, version) must not contain the "$end" keyword.
HeadPtr makeVCDHeader(TimeScale     timescale_quan = TimeScale::ONE,
                      TimeScaleUnit timescale_unit = TimeScaleUnit::ns,
                      const std::string& date = utils::now(),
                      const std::string& comment = "",
                      const std::string& version = "");

// -----------------------------
// Writer of a Value Change Dump file
// A VCD file captures time-ordered changes to the value of variables
class VCDWriter
{
public:
    // Takes ownership of *header* (a null header is replaced by the default one)
    VCDWriter(std::string filename, HeadPtr &&header, TimeStamp init_timestamp = 0u);
    VCDWriter(std::string filename, HeadPtr &header, TimeStamp init_timestamp = 0u)
        : VCDWriter(std::move(filename), std::move(header), init_timestamp) {}
    VCDWriter(VCDWriter&&) = delete;
    VCDWriter(const VCDWriter&) = delete;
    VCDWriter& operator=(const VCDWriter&) = delete;
    VCDWriter& operator=(VCDWriter&&) = delete;

    // Never throws: errors on the implicit close are swallowed.
    // Call `close()` explicitly to observe them.
    virtual ~VCDWriter();

    // Register a VCD variable and return its mark to change value further.
    // Remember, all VCD variables must be registered prior to any value changes.
    // Note, *size* may be `0`, some types ("int", "real", "event") have a default size.
    // Scope and name must consist of printable non-whitespace characters only.
    VarPtr register_var(const std::string &scope,                  // Variable belongs within the hierarchical scope
                        const std::string &name,                   // Human-readable variable idetifier
                        VariableType type = var_def_type,          // Verilog data type of variable
                        unsigned size = 0,                         // Size of variable, in bits
                        const VarValue &init = {VCDValues::UNDEF}, // Initial value (optional)
                        bool duplicate_names_check = true);        // speed-up (optimisation)

    // Change variable's value in VCD stream.
    // Call this method, for all variables changed on this *timestamp*.
    // It is okay to call it multiple times with the same *timestamp*,
    // but never call with a past *timestamp*
    // Return:  *true* if new_value is dumped into VCD file,
    //         *false* if new_value is not changed from priveios *timestamp* for a given var
    // Events are never deduplicated: every call triggers the event.
    bool change(VarPtr var, TimeStamp timestamp, const VarValue &value)
    { return _change(std::move(var), timestamp, value); }

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
    //! get VCD Variable (throws VCDPhaseException if it is not registered)
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
    //! Format and write to the output file
    template <class... Args>
    void _print(std::format_string<Args...> fmt, Args&&... args)
    { std::format_to(std::ostreambuf_iterator<char>(_ofile), fmt, std::forward<Args>(args)...); }

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
};

// -----------------------------
using WriterPtr = std::shared_ptr<VCDWriter>;

}

