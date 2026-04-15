#include "lunaricorn.h"

namespace lunaricorn 
{

std::string current_time_str()
{
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << ','
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace lunaricorn