// mlog.cpp
#include "maintenance.h"
#include <iostream>
#include <sstream>
#include <algorithm>
namespace lunaricorn
{
// Static members definition
std::string MLog::owner;
std::string MLog::token;

std::string MLog::get_caller_info(const std::source_location& loc) {
    // Extract basename from full file path
    std::string_view full_path = loc.file_name();
    auto pos = full_path.find_last_of("/\\");
    std::string_view filename = (pos == std::string_view::npos) ? full_path : full_path.substr(pos + 1);

    std::ostringstream oss;
    oss << filename << ':' << loc.function_name() << ':' << loc.line();
    return oss.str();
}

void MLog::log(std::string_view msg, const std::source_location& loc) {
    if (owner.empty() || token.empty()) {
        std::cerr << "MLog: owner and token must be set before logging\n";
        return;
    }

    std::string caller_info = get_caller_info(loc);
    std::string full_msg = caller_info + " " + std::string(msg);
    std::cout << full_msg << std::endl;
    try {
        LogCollectorClient::instance().send_log(owner, token, full_msg, "log");
    } catch (const std::exception& e) {
        std::cerr << "MLog error: " << e.what() << std::endl;
    }
}

void MLog::d(std::string_view msg, const std::source_location& loc) {
    log(std::string("[ DEBUG ] ") + std::string(msg), loc);
}

void MLog::w(std::string_view msg, const std::source_location& loc) {
    log(std::string("[WARNING] ") + std::string(msg), loc);
}

void MLog::e(std::string_view msg, const std::source_location& loc) {
    log(std::string("[ ERROR ] ") + std::string(msg), loc);
}

} // namespace lunaricorn