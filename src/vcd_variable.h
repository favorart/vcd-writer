#pragma once

#include <string>

#include "vcd_writer.h"

namespace vcd {
// A registered variable: its declaration and the rules of its values
struct VCDVariable
{
    // how the values are written
    enum class Kind { scalar, vector, real, string, event };

    // The variable of *type* with its default *size* if `0` (throws VCDTypeException)
    VCDVariable(std::string var_name, VariableType var_type, unsigned var_size, unsigned var_ident);

    const std::string  name;
    const VariableType type;
    const unsigned     size;   // in bits
    const unsigned     ident;  // ID in the VCD output (written in hex), index in the writer
    const Kind         kind;

    // "$var <type> <size> <ident> <name> $end"
    [[nodiscard]] std::string declaration() const;

    // Validate *value* and write its VCD change record (without ident) into *record*.
    // *record* is a reused buffer (no allocation once its capacity is enough);
    // its contents are unspecified if an exception is thrown.
    void change_record(const VarValue &value, VarValue &record) const;
};
}
