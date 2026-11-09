#include "vcd_utils.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <ctime>
#include <iterator>
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
    std::array<char, 80> buffer{};
    const size_t n = std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return { buffer.data(), n };
}

// -----------------------------
bool validate_date(const std::string &date)
{
    // positions of the fields in "YYYY-MM-DD hh:mm:ss"
    constexpr std::string_view format = "YYYY-MM-DD hh:mm:ss";
    if (date.size() != format.size())
        return false;
    for (size_t i = 0; i < format.size(); ++i)
    {
        const bool digit = (date[i] >= '0' && date[i] <= '9');
        const bool sep = (format[i] == '-' || format[i] == ' ' || format[i] == ':');
        if (sep ? date[i] != format[i] : !digit)
            return false;
    }
    auto field = [&date](size_t pos, size_t len) { return std::stoi(date.substr(pos, len)); };
    const std::chrono::year_month_day ymd{ std::chrono::year{ field(0, 4) },
                                           std::chrono::month{ unsigned(field(5, 2)) },
                                           std::chrono::day{ unsigned(field(8, 2)) } };
    return ymd.ok() && field(11, 2) < 24 && field(14, 2) < 60 && field(17, 2) <= 60; // 60: leap second
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
    char digits[64];
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
