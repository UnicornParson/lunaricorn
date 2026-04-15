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
#else
#define POINT 
#define DPRINT(s) 
#endif // DEBUG

}