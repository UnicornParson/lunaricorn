#pragma once

#include <soci/soci.h>
#include <soci/postgresql/soci-postgresql.h>

#include <boost/json.hpp>

#include <string>
#include <optional>
#include <stdexcept>

#include <lunaricorn.h>

namespace json = boost::json;

class BlobStorage
{
public:
    explicit BlobStorage(const DbConfig& cfg);

    bool contains(const std::string& id);
    bool store(const std::string& id, const json::object& data);
    std::optional<json::object> load(const std::string& id);
    size_t count();
    bool ok();

private:
    soci::session sql;
    bool ok_ = false;

    static std::string json_to_string(const json::value& v);
};