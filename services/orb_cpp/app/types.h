#pragma once
#include <lunaricorn.h>
#include "oid.h"
#include <optional>
#include <boost/json.hpp>

namespace json = boost::json;
struct InternalMetaObject
{
    oid id;
    std::optional<oid> parent;
    std::optional<oid> prev;
    std::optional<oid> next;
    std::vector<std::string> tags;
    std::string description;
    bool has_content;
};

struct OrbData
{
    oid id;
    json::object data;
};

using OrbMeta = InternalMetaObject;


