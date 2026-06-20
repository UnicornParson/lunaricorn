#pragma once

#include "signaling_engine.h"
#include <Poco/Data/TypeHandler.h>
#include <Poco/Data/RecordSet.h>
#include <Poco/Dynamic/Var.h>
#include <boost/json.hpp>

namespace Poco {
namespace Data {

template<>
class TypeHandler<lunaricorn::EventDataExtended>
{
public:
    static std::size_t size();
    static void extract(std::size_t pos, const Poco::Data::RecordSet& rs, lunaricorn::EventDataExtended& obj);
    static void bind(std::size_t pos, const lunaricorn::EventDataExtended& obj, Poco::Data::Statement& stmt);
    static void prepare(std::size_t pos, const lunaricorn::EventDataExtended& obj, Poco::Data::Statement& stmt);
};

} // namespace Data
} // namespace Poco
