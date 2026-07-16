#pragma once

#include <string>
#include <optional>
#include <vector>
#include <memory>

#include <boost/json.hpp>

#include "orb_controller.h"

namespace json = boost::json;

/// High-level object representing an orb entity.
/// It is bound to an IOrbController and stores its data in a local cache.
/// The ID is immutable - set at construction.
class OrbObject {
public:
    /// Construct an OrbObject with a given controller and id.
    /// Does NOT check if the object exists on the server.
    OrbObject(IOrbController& controller, std::string id);

    /// Delete copy but allow move
    OrbObject(const OrbObject&) = delete;
    OrbObject& operator=(const OrbObject&) = delete;
    OrbObject(OrbObject&&) = default;
    OrbObject& operator=(OrbObject&&) = default;

    ~OrbObject() = default;

    /// Get immutable ID
    const std::string& id() const { return id_; }

    /// Check if the object exists on the server (GET /meta/{id})
    bool check();

    // --- Getters (lazy-load from server) ---
    std::optional<std::string> parent();
    std::optional<std::string> prev();
    std::optional<std::string> next();
    std::vector<std::string> tags();
    std::string description();
    bool has_content();

    // --- Setters (write to server, update cache) ---
    void set_parent(const std::optional<std::string>& val);
    void set_prev(const std::optional<std::string>& val);
    void set_next(const std::optional<std::string>& val);
    void set_tags(const std::vector<std::string>& val);
    void set_description(const std::string& val);

    // --- Blob operations ---
    bool store_blob(const json::object& data);
    std::optional<json::object> load_blob();

    // --- Link navigation ---
    /// Returns a new OrbObject for the parent (nullopt if not set or not found)
    std::optional<OrbObject> follow_parent();
    /// Returns a new OrbObject for the previous sibling
    std::optional<OrbObject> follow_prev();
    /// Returns a new OrbObject for the next sibling
    std::optional<OrbObject> follow_next();

private:
    /// Ensure data is loaded from server
    void ensure_loaded();

    IOrbController& controller_;
    std::string id_;

    // Cached data
    bool loaded_ = false;
    std::optional<std::string> parent_;
    std::optional<std::string> prev_;
    std::optional<std::string> next_;
    std::vector<std::string> tags_;
    std::string description_;
    bool has_content_ = false;
};