#include "vcd_header.h"

#include <format>

#include "vcd_utils.h"

namespace vcd {
namespace {
constexpr std::array<const char*, size_t(TimeScaleUnit::_count_)> TIMESCALE_UNITS{ "s", "ms", "us", "ns", "ps", "fs" };

void check_no_end(const std::string &text, const char *what)
{
    if (text.find("$end") != std::string::npos)
        throw TypeException{ std::format("Header {} must not contain '$end'", what) };
}
}

// -----------------------------
void HeaderDeleter::operator()(Header *p) const { delete p; }

// -----------------------------
HeadPtr make_header(TimeScale timescale_quan, TimeScaleUnit timescale_unit, const std::string &date,
                      const std::string &comment, const std::string &version)
{
    if (timescale_quan != TimeScale::one && timescale_quan != TimeScale::ten && timescale_quan != TimeScale::hundred)
        throw TypeException{ std::format("Invalid time scale quant {}", int(timescale_quan)) };
    if (timescale_unit < TimeScaleUnit::s || timescale_unit >= TimeScaleUnit::_count_)
        throw TypeException{ std::format("Invalid time scale unit {}", int(timescale_unit)) };
    if (!date.empty() && !utils::validate_date(date))
        throw TypeException{ std::format("Invalid date '{}', expected '{}'", date, utils::DATE_FORMAT) };
    check_no_end(comment, "comment");
    check_no_end(version, "version");

    HeadPtr head{ new Header };
    head->values[Header::timescale] = std::to_string(int(timescale_quan)) + " " + TIMESCALE_UNITS[size_t(timescale_unit)];
    head->values[Header::date] = date;
    head->values[Header::comment] = comment;
    head->values[Header::version] = version;
    return head;
}
}
