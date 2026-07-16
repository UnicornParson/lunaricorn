#pragma once

#include <string>
#include <optional>
#include <vector>

#include <boost/json.hpp>

namespace json = boost::json;

/// Internal meta object data returned by IOrbController
struct OrbMetaData {
    std::string id;
    std::optional<std::string> parent;
    std::optional<std::string> prev;
    std::optional<std::string> next;
    std::vector<std::string> tags;
    std::string description;
    bool has_content = false;
};

/// Abstract interface for orb server communication
class IOrbController {
public:
    virtual ~IOrbController() = default;

    /// Health check
    virtual bool health() = 0;

    /// Get meta object by id
    virtual std::optional<OrbMetaData> get_meta(const std::string& id) = 0;

    /// Store/update meta object
    virtual bool put_meta(const std::string& id, const OrbMetaData& data) = 0;

    /// Get blob data by id
    virtual std::optional<json::object> get_blob(const std::string& id) = 0;

    /// Store blob data
    virtual bool put_blob(const std::string& id, const json::object& data) = 0;

    /// Search by tags
    virtual std::vector<OrbMetaData> search_by_tags(const std::vector<std::string>& tags) = 0;

    /// Generate new ID
    virtual std::string generate_id() = 0;
};