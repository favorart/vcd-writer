#pragma once

#include <array>
#include <string>

#include "vcd_writer.h"

namespace vcd {
// Values of the VCD header sections, validated by `make_header()`
struct Header
{
    // sections in the output order
    enum Section : size_t { timescale, date, comment, version, _count_ };

    static constexpr std::array<const char*, _count_> keywords{ "$timescale", "$date", "$comment", "$version" };
    std::array<std::string, _count_> values;  // empty values are not written
};
}
