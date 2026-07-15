#pragma once

#include <soci/soci.h>
#include <soci/postgresql/soci-postgresql.h>

#include <boost/json.hpp>

#include <string>
#include <vector>
#include <optional>
#include <stdexcept>

#include <lunaricorn.h>
#include "types.h"

class MetaStorage
{
public:
    explicit MetaStorage(const DbConfig& cfg);

    bool contains(const std::string& id);
    bool store(const std::string& id, const InternalMetaObject& data);
    std::optional<InternalMetaObject> load(const std::string& id);
    size_t count();
    bool ok();

private:
    soci::session sql;
    bool ok_ = false;

    static std::string tags_to_pg_array(const std::vector<std::string>& tags);
    static std::vector<std::string> pg_array_to_tags(const std::string& tags_str);
};