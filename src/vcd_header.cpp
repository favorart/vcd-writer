#include "vcd_header.h"

#include <format>

#include "vcd_utils.h"

namespace vcd {
namespace {
constexpr std::array<const char*, size_t(TimeScaleUnit::_count_)> TIMESCALE_UNITS{ "s", "ms", "us", "ns", "ps", "fs" };

void check_no_end(const std::string &text, const char *what)
{
    if (text.find("$end") != std::string::npos)
        throw VCDTypeException{ std::format("Header {} must not contain '$end'", what) };
}
}

// -----------------------------
void VCDHeaderDeleter::operator()(VCDHeader *p) const { delete p; }

// -----------------------------
HeadPtr makeVCDHeader(TimeScale timescale_quan, TimeScaleUnit timescale_unit, const std::string &date,
                      const std::string &comment, const std::string &version)
{
    if (timescale_quan != TimeScale::ONE && timescale_quan != TimeScale::TEN && timescale_quan != TimeScale::HUNDRED)
        throw VCDTypeException{ std::format("Invalid time scale quant {}", int(timescale_quan)) };
    if (timescale_unit < TimeScaleUnit::s || timescale_unit >= TimeScaleUnit::_count_)
        throw VCDTypeException{ std::format("Invalid time scale unit {}", int(timescale_unit)) };
    if (!date.empty() && !utils::validate_date(date))
        throw VCDTypeException{ std::format("Invalid date '{}', expected 'YYYY-MM-DD hh:mm:ss'", date) };
    check_no_end(comment, "comment");
    check_no_end(version, "version");

    auto timescale = std::to_string(int(timescale_quan)) + " " + TIMESCALE_UNITS[size_t(timescale_unit)];
    return HeadPtr{ new VCDHeader{ { std::move(timescale), date, comment, version } } };
}
}
