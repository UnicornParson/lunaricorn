#include <iostream>
#include <boost/algorithm/string.hpp>
#include <Poco/DateTime.h>
#include <Poco/DateTimeFormatter.h>

int main() {
    // Test Boost: string to uppercase
    std::string msg = "hello boost and poco";
    std::string upper = boost::algorithm::to_upper_copy(msg);
    std::cout << "Boost test: " << upper << std::endl;

    // Test POCO: current date and time formatting
    Poco::DateTime now;
    std::string dt = Poco::DateTimeFormatter::format(now, "%Y-%m-%d %H:%M:%S");
    std::cout << "POCO test: Current date/time is " << dt << std::endl;

    return 0;
}