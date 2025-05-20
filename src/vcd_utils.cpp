#include <array>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>
#ifdef _MSC_VER
#pragma warning (disable : 4996)
#endif



namespace vcd::utils {
// -----------------------------
std::string format(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::vector<char> v(1024);
    while (true)
    {
        va_list args2;
        va_copy(args2, args);
        int res = vsnprintf(v.data(), v.size(), fmt, args2);
        if (res >= 0 && res < static_cast<int>(v.size()))
        {
            va_end(args);
            va_end(args2);
            return {v.data()};
        }
        size_t size = 0;
        if (res < 0)
            size = v.size() * 2;
        else
            size = static_cast<size_t>(res) + 1;
        v.clear();
        v.resize(size);
        va_end(args2);
    }
}

// -----------------------------
std::string now()
{
    std::time_t rawtime = 0;
    struct tm *timeinfo = nullptr;
    std::array<char, 80> buffer{};
    std::time(&rawtime);
    #if defined(_WIN32) || defined(_MSC_VER)
        struct tm timeinfo_storage;
        localtime_s(&timeinfo_storage, &rawtime);
        timeinfo = &timeinfo_storage;
    #else
        struct tm timeinfo_storage{};
        timeinfo = localtime_r(&rawtime, &timeinfo_storage);
    #endif
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S", timeinfo);
    return {buffer.data(), std::strlen(buffer.data())};
}

// -----------------------------
bool validate_date([[maybe_unused]]const std::string &date)
{
    // TODO: validate format
    return true;
}

// -----------------------------
void replace_new_lines(std::string &str, const std::string &sub)
{
    bool nl = false;
    int k = 0;
    for (auto j = 0u; j < str.size(); ++j)
    {
        if (str[j] == '\n' || str[j] == '\r')
        {
            if (!nl) // new_lines may be 2-chars length
            { for (auto c : sub) str[k++] = c; }
            nl = !nl;
        }
        else
        {
            nl = false;
            str[k++] = str[j];
        }
    }
}

// -----------------------------
}

   