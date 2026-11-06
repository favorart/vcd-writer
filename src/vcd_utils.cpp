#include <array>
#include <cstring>
#include <string>
#include <ctime>



namespace vcd::utils {
// -----------------------------
std::string now()
{
    std::time_t rawtime = 0;
    struct tm *timeinfo = nullptr;
    std::array<char, 80> buffer{};
    std::time(&rawtime);
    timeinfo = std::localtime(&rawtime);
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

   