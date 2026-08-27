#include "engine.h"
#include "signal_connector.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <sstream>

Engine::Engine(const lunaricorn::DbConfig& cfg)
    : blobs_(cfg)
    , metas_(cfg)
{
}

bool Engine::store_blob(const std::string& id, const json::object& data)
{
    // Verify that meta exists for this id
    if(!contains_meta(id)) {
        MLOG_E("Engine::store_blob({}): meta not found, blob without meta is not allowed", id);
        return false;
    }

    bool result = blobs_.store(id, data);
    if(result) {
        update_has_blob(id);
        // Notify signaling about blob update
        notify_signaling(lunaricorn::internal::SignalingEventType::FileOp_update, id, id);
    }
    return result;
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
    // Auto-detect has_content based on blob existence
    InternalMetaObject updated_meta = meta;
    updated_meta.has_content = contains_blob(id);
    
    // Determine if this is a new or update operation BEFORE storing
    bool is_new = !contains_meta(id);
    
    bool result = metas_.store(id, updated_meta);
    if(result) {
        // Signal based on whether this was a new or existing object
        const auto& event_type = is_new 
            ? lunaricorn::internal::SignalingEventType::FileOp_new 
            : lunaricorn::internal::SignalingEventType::FileOp_update;
        notify_signaling(event_type, id, id);
    }
    return result;
}

std::optional<InternalMetaObject> Engine::load_meta(const std::string& id)
{
    return metas_.load(id);
}

bool Engine::contains_meta(const std::string& id)
{
    return metas_.contains(id);
}

std::vector<InternalMetaObject> Engine::search_by_tags(const std::vector<std::string>& tags)
{
    return metas_.search_by_tags(tags);
}

std::string Engine::generate_id()
{
    static boost::uuids::random_generator gen;
    boost::uuids::uuid u = gen();
    return boost::uuids::to_string(u);
}

bool Engine::ok()
{
    return blobs_.ok() && metas_.ok();
}

void Engine::set_signal_connector(SignalConnector* sc)
{
    _signal_connector = sc;
}

void Engine::notify_signaling(const std::string& event_type, const std::string& id, const std::string& uuid)
{
    if(_signal_connector && _signal_connector->ready()) {
        _signal_connector->push_event(event_type, id, uuid);
    }
}

void Engine::update_has_blob(const std::string& id)
{
    auto meta = metas_.load(id);
    if(!meta) return;

    bool has_blob = blobs_.contains(id);
    if(meta->has_content != has_blob) {
        meta->has_content = has_blob;
        metas_.store(id, *meta);
        MLOG_D("Engine::update_has_blob({}): set has_content={}", id, has_blob);
    }
}