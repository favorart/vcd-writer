#pragma once

#include <array>
#include <string>

#include "vcd_writer.h"

namespace vcd {
// Values of the VCD header sections, validated by `makeVCDHeader()`
struct VCDHeader
{
    static constexpr std::array<const char*, 4> keywords{ "$timescale", "$date", "$comment", "$version" };
    std::array<std::string, 4> values;  // empty values are not written
};
}
