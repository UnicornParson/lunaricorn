#pragma once

#include <string>
#include <optional>
#include <vector>

#include <lunaricorn.h>
#include "blob_storage.h"
#include "meta_storage.h"

class Engine
{
public:
    explicit Engine(const lunaricorn::DbConfig& cfg);

    // Blob operations
    bool store_blob(const std::string& id, const json::object& data);
    std::optional<json::object> load_blob(const std::string& id);
    bool contains_blob(const std::string& id);

    // Meta operations
    bool store_meta(const std::string& id, const InternalMetaObject& meta);
    std::optional<InternalMetaObject> load_meta(const std::string& id);
    bool contains_meta(const std::string& id);
    std::vector<InternalMetaObject> search_by_tags(const std::vector<std::string>& tags);

    // ID generation
    std::string generate_id();

    // Status
    bool ok();

private:
    BlobStorage blobs_;
    MetaStorage metas_;

    void update_has_blob(const std::string& id);
};