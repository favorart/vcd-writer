/**
 * @file vcd_writer_c.h
 * @brief C interface of the VCD writer (a thin layer over the C++ `vcd::Writer`).
 *
 * Every function returns a status; on an error vcd_last_error() describes it.
 * Strings are NUL-terminated. Pointer arguments must not be NULL unless stated otherwise.
 */
#ifndef VCD_WRITER_C_H
#define VCD_WRITER_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Writer of a VCD file (opaque).
typedef struct vcd_writer vcd_writer;
/// Handle of a registered variable (opaque), owned by its writer.
typedef struct vcd_var vcd_var;

/// Result of a call.
typedef enum vcd_status
{
    vcd_ok = 0,
    vcd_error_argument,  ///< NULL pointer argument
    vcd_error_phase,     ///< wrong call order (see vcd::PhaseException)
    vcd_error_type,      ///< invalid name, value, size, type (see vcd::TypeException)
    vcd_error_io,        ///< the file can't be opened or written
    vcd_error_memory,    ///< out of memory
    vcd_error_unknown
} vcd_status;

/// VCD scope types (same as vcd::ScopeType).
typedef enum vcd_scope_type
{
    vcd_scope_begin, vcd_scope_fork, vcd_scope_function, vcd_scope_module, vcd_scope_task,
    vcd_scope_count_
} vcd_scope_type;

/// VCD variable types (same as vcd::VariableType).
typedef enum vcd_var_type
{
    vcd_var_wire, vcd_var_reg, vcd_var_string, vcd_var_parameter, vcd_var_integer, vcd_var_real,
    vcd_var_realtime, vcd_var_time, vcd_var_event, vcd_var_supply0, vcd_var_supply1, vcd_var_tri,
    vcd_var_triand, vcd_var_trior, vcd_var_trireg, vcd_var_tri0, vcd_var_tri1, vcd_var_wand, vcd_var_wor,
    vcd_var_count_
} vcd_var_type;

/// Timescale numbers (same as vcd::TimeScale).
typedef enum vcd_timescale
{
    vcd_timescale_one = 1,
    vcd_timescale_ten = 10,
    vcd_timescale_hundred = 100
} vcd_timescale;

/// Timescale units (same as vcd::TimeScaleUnit).
typedef enum vcd_timescale_unit
{
    vcd_unit_s, vcd_unit_ms, vcd_unit_us, vcd_unit_ns, vcd_unit_ps, vcd_unit_fs,
    vcd_unit_count_
} vcd_timescale_unit;

/**
 * @brief Create (truncate) a VCD file.
 * @param[out] writer   The new writer, NULL on an error.
 * @param filename      Output file.
 * @param quan          Timescale number.
 * @param unit          Timescale unit.
 * @param date          "YYYY-MM-DD hh:mm:ss", "" for no `$date`, NULL for the current time.
 * @param comment       `$comment` text, NULL or "" for none.
 * @param version       `$version` text, NULL or "" for none.
 * @param init_timestamp Time of the initial values (`$dumpvars`).
 */
vcd_status vcd_open(vcd_writer **writer, const char *filename, vcd_timescale quan, vcd_timescale_unit unit,
                    const char *date, const char *comment, const char *version, uint64_t init_timestamp);

/**
 * @brief Flush and close the file, then free the writer and its variables.
 * @param writer    The writer, NULL does nothing. It is freed even on an error.
 * @param timestamp A later time to write, NULL for none.
 */
vcd_status vcd_close(vcd_writer *writer, const uint64_t *timestamp);

/**
 * @brief Register a variable (see vcd::Writer::register_var()).
 * @param[out] var The variable handle, valid until vcd_close().
 * @param size     Size in bits, 0 for the default size of the type.
 * @param init     Initial value, NULL for `x`.
 */
vcd_status vcd_register_var(vcd_writer *writer, const char *scope, const char *name, vcd_var_type type,
                            unsigned size, const char *init, vcd_var **var);

/**
 * @brief Change the value of a variable (see vcd::Writer::change()).
 * @param[out] changed 1 if the value is written, 0 if it equals the previous one; may be NULL.
 */
vcd_status vcd_change(vcd_writer *writer, const vcd_var *var, uint64_t timestamp, const char *value, int *changed);

/// @brief Change the value of the variable *name* in *scope*.
/// @copydetails vcd_change()
vcd_status vcd_change_by_name(vcd_writer *writer, const char *scope, const char *name, uint64_t timestamp,
                              const char *value, int *changed);

/// @brief Suspend dumping (`$dumpoff`) from *timestamp*.
vcd_status vcd_dump_off(vcd_writer *writer, uint64_t timestamp);
/// @brief Resume dumping (`$dumpon`) from *timestamp*.
vcd_status vcd_dump_on(vcd_writer *writer, uint64_t timestamp);

/// @brief Write the buffered output to the file; *timestamp* (may be NULL) is a later time to write.
vcd_status vcd_flush(vcd_writer *writer, const uint64_t *timestamp);

/// @brief Set the type of an existing *scope*.
vcd_status vcd_set_scope_type(vcd_writer *writer, const char *scope, vcd_scope_type type);
/// @brief Type of the scopes registered further (default: module).
vcd_status vcd_set_scope_default_type(vcd_writer *writer, vcd_scope_type type);
/// @brief Separator of the scope names (default: ".").
vcd_status vcd_set_scope_sep(vcd_writer *writer, const char *scope_sep);

/// @return The message of the last error in the calling thread ("" if none); valid until the next call.
const char* vcd_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
