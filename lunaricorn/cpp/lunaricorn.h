#pragma once
#include "stdafx.h"
#include "config.h"
#include "maintenance.h"

namespace lunaricorn
{

std::string current_time_str();

#if DEBUG
#define DPRINT(s) std::cout << "[" << current_time_str() << "] point " << __FILE__ << " f:(" << __FUNCTION__ << ")." << __LINE__ << " : " << s << std::endl << std::flush
#define POINT std::cerr << "[" << current_time_str() << "] point " << __FILE__ << " f:(" << __FUNCTION__ << ")." << __LINE__ << std::endl << std::flush
#define PRINT_SESSION_COUNTER(prefix, sc) std::cout << "on session " << prefix << " count: " << (sc) << std::endl
#else
#define POINT 
#define DPRINT(s) 
#define PRINT_SESSION_COUNTER(prefix, sc)
#endif // DEBUG

#define LOG_ACCESS(msg) \
    do { \
        std::cout << "[" << current_time_str() << "] " << msg << std::endl << std::flush; \
    } while(0)
}