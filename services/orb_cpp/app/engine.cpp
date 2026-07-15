#include "engine.h"

Engine::Engine(const DbConfig& cfg)
    : blobs_(cfg)
    , metas_(cfg)
{
}

bool Engine::store_blob(const std::string& id, const json::object& data)
{
    return blobs_.store(id, data);
}

std::optional<json::object> Engine::load_blob(const std::string& id)
{
    return blobs_.load(id);
}

bool Engine::contains_blob(const std::string& id)
{
    return blobs_.contains(id);
}

bool Engine::store_meta(const std::string& id, const InternalMetaObject& meta)
{
    return metas_.store(id, meta);
}

std::optional<InternalMetaObject> Engine::load_meta(const std::string& id)
{
    return metas_.load(id);
}

bool Engine::contains_meta(const std::string& id)
{
    return metas_.contains(id);
}

bool Engine::ok()
{
    return blobs_.ok() && metas_.ok();
}