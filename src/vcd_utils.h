#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vcd_writer.h"

// Internal helpers of the library
namespace vcd::utils {
// Locale-independent ASCII checks (std::tolower/isgraph depend on the C locale and are slower)
constexpr char to_lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
constexpr bool is_graph(char c) { return c > ' ' && c < '\x7f'; }
constexpr bool is_bit(char c) { return c == Bit::one || c == Bit::zero || c == Bit::undef || c == Bit::highz; }

// Non-empty, printable, without whitespace and does not look like a keyword ("$end", "$var", ...)
bool is_valid_token(const std::string &s);
// Parts of *s* between the separators
std::vector<std::string> split(const std::string &s, const std::string &sep);
// Key of a variable: scope + '\0' + name, written into the reused *key*
void make_var_key(std::string &key, const std::string &scope, const std::string &name);
// Append the unsigned *value* in the given base (lowercase, same as "{:d}"/"{:x}")
void append_number(std::string &out, uint64_t value, int base);
// "YYYY-MM-DD hh:mm:ss" with a valid calendar date and time
bool validate_date(const std::string &date);
// Every line break ("\n", "\r" or "\r\n") becomes "\n\t"
std::string indent_lines(const std::string &str);
}
