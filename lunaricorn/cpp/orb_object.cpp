#include "orb_object.h"

#include <lunaricorn.h>

OrbObject::OrbObject(IOrbController& controller, std::string id)
    : controller_(controller)
    , id_(std::move(id))
{
}

bool OrbObject::check()
{
    auto meta = controller_.get_meta(id_);
    return meta.has_value();
}

void OrbObject::ensure_loaded()
{
    if(loaded_)
        return;

    auto meta = controller_.get_meta(id_);
    if(meta) {
        parent_ = std::move(meta->parent);
        prev_ = std::move(meta->prev);
        next_ = std::move(meta->next);
        tags_ = std::move(meta->tags);
        description_ = std::move(meta->description);
        has_content_ = meta->has_content;
        loaded_ = true;
    }
}

// --- Getters ---

std::optional<std::string> OrbObject::parent()
{
    ensure_loaded();
    return parent_;
}

std::optional<std::string> OrbObject::prev()
{
    ensure_loaded();
    return prev_;
}

std::optional<std::string> OrbObject::next()
{
    ensure_loaded();
    return next_;
}

std::vector<std::string> OrbObject::tags()
{
    ensure_loaded();
    return tags_;
}

std::string OrbObject::description()
{
    ensure_loaded();
    return description_;
}

bool OrbObject::has_content()
{
    ensure_loaded();
    return has_content_;
}

// --- Setters ---

void OrbObject::set_parent(const std::optional<std::string>& val)
{
    OrbMetaData data;
    data.id = id_;
    data.parent = val;
    data.prev = prev_;
    data.next = next_;
    data.tags = tags_;
    data.description = description_;

    if(controller_.put_meta(id_, data)) {
        parent_ = val;
    }
}

void OrbObject::set_prev(const std::optional<std::string>& val)
{
    OrbMetaData data;
    data.id = id_;
    data.parent = parent_;
    data.prev = val;
    data.next = next_;
    data.tags = tags_;
    data.description = description_;

    if(controller_.put_meta(id_, data)) {
        prev_ = val;
    }
}

void OrbObject::set_next(const std::optional<std::string>& val)
{
    OrbMetaData data;
    data.id = id_;
    data.parent = parent_;
    data.prev = prev_;
    data.next = val;
    data.tags = tags_;
    data.description = description_;

    if(controller_.put_meta(id_, data)) {
        next_ = val;
    }
}

void OrbObject::set_tags(const std::vector<std::string>& val)
{
    OrbMetaData data;
    data.id = id_;
    data.parent = parent_;
    data.prev = prev_;
    data.next = next_;
    data.tags = val;
    data.description = description_;

    if(controller_.put_meta(id_, data)) {
        tags_ = val;
    }
}

void OrbObject::set_description(const std::string& val)
{
    OrbMetaData data;
    data.id = id_;
    data.parent = parent_;
    data.prev = prev_;
    data.next = next_;
    data.tags = tags_;
    data.description = val;

    if(controller_.put_meta(id_, data)) {
        description_ = val;
    }
}

// --- Blob operations ---

bool OrbObject::store_blob(const json::object& data)
{
    bool result = controller_.put_blob(id_, data);
    if(result) {
        has_content_ = true;
        loaded_ = true;
    }
    return result;
}

std::optional<json::object> OrbObject::load_blob()
{
    return controller_.get_blob(id_);
}

// --- Link navigation ---

std::optional<OrbObject> OrbObject::follow_parent()
{
    ensure_loaded();
    if(!parent_ || parent_->empty())
        return std::nullopt;

    OrbObject child(controller_, *parent_);
    if(child.check())
        return child;

    return std::nullopt;
}

std::optional<OrbObject> OrbObject::follow_prev()
{
    ensure_loaded();
    if(!prev_ || prev_->empty())
        return std::nullopt;

    OrbObject sibling(controller_, *prev_);
    if(sibling.check())
        return sibling;

    return std::nullopt;
}

std::optional<OrbObject> OrbObject::follow_next()
{
    ensure_loaded();
    if(!next_ || next_->empty())
        return std::nullopt;

    OrbObject sibling(controller_, *next_);
    if(sibling.check())
        return sibling;

    return std::nullopt;
}