#include <array>
#include <cstring>
#include <string>
#include <mutex>
#include <ctime>
#include <utility>



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
    return {buffer.data(), n};
}

// -----------------------------
bool validate_date([[maybe_unused]]const std::string &date)
{
    // TODO: validate format
    return true;
}

// -----------------------------
// Replace every line break ("\n", "\r" or "\r\n") by *sub*
void replace_new_lines(std::string &str, const std::string &sub)
{
    std::string res;
    res.reserve(str.size());
    for (size_t j = 0; j < str.size(); ++j)
    {
        if (str[j] == '\r' || str[j] == '\n')
        {
            if (str[j] == '\r' && j + 1 < str.size() && str[j + 1] == '\n')
                ++j; // "\r\n" is a single line break
            res += sub;
        }
        else
            res += str[j];
    }
    str = std::move(res);
}

// -----------------------------
}

   