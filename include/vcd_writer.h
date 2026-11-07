/**
 * @file vcd_writer.h
 * @brief Writer of Value Change Dump (VCD) files, IEEE 1364-2005, 18.2.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace vcd {
namespace utils {
/// @return The current local time as "YYYY-MM-DD hh:mm:ss".
std::string now();
}

// -----------------------------
// `_count_` is not a value: it is the number of values (the first invalid one).

/// Valid VCD scope types.
enum class ScopeType : char
{ begin, fork, function, module, task, _count_ };

/// Valid VCD variable types.
enum class VariableType : char
{ wire, reg, string, parameter, integer, real, realtime, time, event,
  supply0, supply1, tri, triand, trior, trireg, tri0, tri1, wand, wor, _count_ };

/// Valid timescale numbers.
enum class TimeScale : char
{ one = 1, ten = 10, hundred = 100, _count_ };

/// Valid timescale units.
enum class TimeScaleUnit : char
{ s, ms, us, ns, ps, fs, _count_ };

/// 4-state values of a bit.
enum Bit : char
{ one = '1', zero = '0', undef = 'x', highz = 'z', _count_ };

/// Simulation time, in timescale units.
using TimeStamp = uint64_t;
/// Value of a variable as text: bits ("1x0z"), a real number ("1.5") or a string.
using VarValue = std::string;

// -----------------------------
/// Base of all errors of the library.
struct Exception : std::runtime_error
{
    explicit Exception(const std::string &message) : std::runtime_error("VCD error: " + message) {}
};

/// Wrong call order: registration after dumping started, change after close, past timestamp...
struct PhaseException : Exception
{
    using Exception::Exception;
};

/// Invalid argument: name, value, size, type...
struct TypeException : Exception
{
    using Exception::Exception;
};

// -----------------------------
struct Variable;
/// Handle of a registered variable.
using VarPtr = std::shared_ptr<Variable>;

struct Header;
/// Deleter of the opaque Header.
struct HeaderDeleter { void operator()(Header *p) const; };
/// Header of a VCD file, made by make_header().
using HeadPtr = std::unique_ptr<Header, HeaderDeleter>;

/**
 * @brief Make a validated VCD header.
 * @param timescale_quan Number of the timescale: 1, 10 or 100.
 * @param timescale_unit Unit of the timescale.
 * @param date    "YYYY-MM-DD hh:mm:ss" or empty (no `$date` section).
 * @param comment Free text, may be multi-line (empty: no `$comment` section).
 * @param version Free text, may be multi-line (empty: no `$version` section).
 * @throws TypeException Invalid timescale or date, a value contains "$end".
 */
HeadPtr make_header(TimeScale     timescale_quan = TimeScale::one,
                      TimeScaleUnit timescale_unit = TimeScaleUnit::ns,
                      const std::string &date = utils::now(),
                      const std::string &comment = "",
                      const std::string &version = "");

// -----------------------------
/**
 * @brief Writer of a Value Change Dump file.
 *
 * A VCD file captures time-ordered changes to the value of variables.
 * Register all variables first, then change their values with non-decreasing timestamps.
 * The output is buffered: call flush() or close() to write it to the file.
 */
class Writer final
{
public:
    /**
     * @brief Create (truncate) the file.
     * @param filename       Output file.
     * @param header         Header, the writer takes its ownership (null: the default one).
     * @param init_timestamp Time of the initial values (`$dumpvars`).
     * @throws Exception The file can't be opened.
     */
    Writer(std::string filename, HeadPtr &&header, TimeStamp init_timestamp = 0u);
    /// @copydoc Writer(std::string, HeadPtr&&, TimeStamp)
    Writer(std::string filename, HeadPtr &header, TimeStamp init_timestamp = 0u)
        : Writer(std::move(filename), std::move(header), init_timestamp) {}
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    /// @brief Close the file. Never throws: call close() explicitly to observe the errors.
    ~Writer();

    /**
     * @brief Register a variable.
     *
     * All variables must be registered before any value change at a later timestamp.
     * @param scope Hierarchical scope, e.g. "top.cpu" (see set_scope_sep()).
     * @param name  Name of the variable in the scope.
     * @param type  Verilog data type.
     * @param size  Size in bits; `0` for the default size of integer, time, real,
     *              realtime (64), string and event (1). Other types require it.
     * @param init  Initial value (`x` of a real is 0).
     * @return Handle to change the value.
     * @throws TypeException  Invalid scope or name (only printable non-whitespace characters),
     *                           duplicate variable, missing size, invalid initial value.
     * @throws PhaseException Registration is finished or the writer is closed.
     */
    VarPtr register_var(const std::string &scope,
                        const std::string &name,
                        VariableType type = VariableType::integer,
                        unsigned size = 0,
                        const VarValue &init = { Bit::undef });

    /**
     * @brief Change the value of a variable.
     * @param var       Variable registered in this writer.
     * @param timestamp Current or a later time (never a past one).
     * @param value     Bits of a scalar or a vector (shorter vectors are left-extended,
     *                  empty is `x`), a real number or a non-empty string without whitespace.
     * @return true if the value is written, false if it equals the previous one.
     *         Events are never deduplicated: every call triggers the event.
     * @throws TypeException  Invalid value, the variable is not registered in this writer.
     * @throws PhaseException Past timestamp or the writer is closed.
     */
    bool change(const VarPtr &var, TimeStamp timestamp, const VarValue &value);
    /// @brief Change the value of the variable *name* in *scope*.
    /// @copydetails change(const VarPtr&, TimeStamp, const VarValue&)
    bool change(const std::string &scope, const std::string &name, TimeStamp timestamp, const VarValue &value);

    /// @brief Suspend dumping (`$dumpoff`) from *timestamp*.
    void dump_off(TimeStamp timestamp);
    /// @brief Resume dumping (`$dumpon` with the current values) from *timestamp*.
    void dump_on(TimeStamp timestamp);

    /**
     * @brief Write the buffered output to the file.
     *
     * Writes the header if it is not written yet: no more variables can be registered.
     * @param timestamp A later time to write, optional.
     * @throws PhaseException The writer is closed.
     */
    void flush(const TimeStamp *timestamp = nullptr);
    /**
     * @brief Flush and close the file.
     *
     * No registration or value change is accepted after it; a repeated call does nothing.
     * @param timestamp A later time to write, optional.
     */
    void close(const TimeStamp *timestamp = nullptr);

    /// @brief Set the type of an existing *scope* (viewers may display them differently).
    /// @throws PhaseException No such scope.
    void set_scope_type(const std::string &scope, ScopeType type);
    /// @brief Type of the scopes registered further (default: module).
    void set_scope_default_type(ScopeType type);
    /// @brief Separator of the scope names in a hierarchical scope (default: "."); empty is ignored.
    void set_scope_sep(const std::string &scope_sep);

    /// @return The variable *name* in *scope*.
    /// @throws PhaseException The variable is not registered.
    VarPtr var(const std::string &scope, const std::string &name) const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

/// Shared handle of a writer.
using WriterPtr = std::shared_ptr<Writer>;

}
