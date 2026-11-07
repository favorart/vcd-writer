#include "vcd_utils.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <ctime>
#include <iterator>
#include <limits>
#include <mutex>

namespace vcd::utils {
// -----------------------------
std::string now()
{
    // std::localtime returns a pointer to a shared static object
    static std::mutex localtime_mutex;
    const std::time_t rawtime = std::time(nullptr);
    std::tm timeinfo{};
    {
        std::lock_guard<std::mutex> lock(localtime_mutex);
        const std::tm *tm = std::localtime(&rawtime);
        if (tm == nullptr)
            return {};
        timeinfo = *tm;
    }
    std::array<char, DATE_FORMAT.size() + 1> buffer{}; // + the terminating '\0'
    const size_t n = std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return { buffer.data(), n };
}

// -----------------------------
bool validate_date(const std::string &date)
{
    using namespace std::chrono;
    if (date.size() != DATE_FORMAT.size())
        return false;
    for (size_t i = 0; i < DATE_FORMAT.size(); ++i)
    {
        const bool digit = (date[i] >= '0' && date[i] <= '9');
        const bool sep = (DATE_FORMAT[i] == '-' || DATE_FORMAT[i] == ' ' || DATE_FORMAT[i] == ':');
        if (sep ? date[i] != DATE_FORMAT[i] : !digit)
            return false;
    }
    // the value of the *field* ("YYYY", "MM", ...) of the format
    auto value = [&date](std::string_view field) {
        return std::stoi(date.substr(DATE_FORMAT.find(field), field.size()));
    };
    const year_month_day ymd{ year{ value("YYYY") }, month{ unsigned(value("MM")) }, day{ unsigned(value("DD")) } };
    return ymd.ok()
        && hours{ value("hh") } < days{ 1 }
        && minutes{ value("mm") } < hours{ 1 }
        && seconds{ value("ss") } <= minutes{ 1 }; // a leap second is 60
}

// -----------------------------
bool is_valid_token(const std::string &s)
{
    return !s.empty() && s[0] != '$' && std::all_of(s.begin(), s.end(), is_graph);
}

// -----------------------------
std::vector<std::string> split(const std::string &s, const std::string &sep)
{
    std::vector<std::string> parts;
    for (size_t beg = 0;;)
    {
        const size_t end = s.find(sep, beg);
        parts.push_back(s.substr(beg, end - beg));
        if (end == std::string::npos)
            return parts;
        beg = end + sep.size();
    }
}

// -----------------------------
void make_var_key(std::string &key, const std::string &scope, const std::string &name)
{
    key.assign(scope);
    key += '\0';
    key += name;
}

// -----------------------------
void append_number(std::string &out, uint64_t value, int base)
{
    char digits[std::numeric_limits<uint64_t>::digits]; // the longest one is in base 2
    const auto res = std::to_chars(std::begin(digits), std::end(digits), value, base);
    out.append(digits, res.ptr);
}

// -----------------------------
std::string indent_lines(const std::string &str)
{
    std::string res;
    res.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i)
    {
        if (str[i] == '\r' || str[i] == '\n')
        {
            if (str[i] == '\r' && i + 1 < str.size() && str[i + 1] == '\n')
                ++i; // "\r\n" is a single line break
            res += "\n\t";
        }
        else
            res += str[i];
    }
    return res;
}
}
