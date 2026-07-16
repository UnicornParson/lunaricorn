#include "meta_storage.h"

#include <sstream>

MetaStorage::MetaStorage(const lunaricorn::DbConfig& cfg)
{
    if(!cfg.valid()) {
        throw std::invalid_argument("invalid db config");
    }

    try {
        std::ostringstream conn;
        conn << "host=" << cfg.dbHost
             << " port=" << cfg.dbPort
             << " user=" << cfg.dbUser
             << " password=" << cfg.dbPassword
             << " dbname=" << cfg.dbDbname;
        sql.open(soci::postgresql, conn.str());

        sql << R"(
            CREATE TABLE IF NOT EXISTS orb_meta (
                id          VARCHAR(128) PRIMARY KEY,
                parent      VARCHAR(128),
                prev        VARCHAR(128),
                next        VARCHAR(128),
                tags        TEXT[],
                description TEXT,
                has_content BOOLEAN NOT NULL DEFAULT FALSE
            )
        )";

        ok_ = true;
        MLOG_D("MetaStorage: connected, table orb_meta ready");
    } catch(const std::exception& e) {
        ok_ = false;
        MLOG_E("MetaStorage: connection failed: {}", e.what());
        throw;
    }
}

std::string MetaStorage::tags_to_pg_array(const std::vector<std::string>& tags)
{
    if(tags.empty())
        return "{}";

    std::ostringstream oss;
    oss << '{';
    for(size_t i = 0; i < tags.size(); ++i) {
        if(i > 0)
            oss << ',';
        oss << '"';
        for(char c : tags[i]) {
            if(c == '"' || c == '\\')
                oss << '\\';
            oss << c;
        }
        oss << '"';
    }
    oss << '}';
    return oss.str();
}

std::vector<std::string> MetaStorage::pg_array_to_tags(const std::string& tags_str)
{
    std::vector<std::string> tags;
    if(tags_str.empty() || tags_str == "{}")
        return tags;

    if(tags_str.front() != '{' || tags_str.back() != '}')
        return tags;

    const std::string content = tags_str.substr(1, tags_str.length() - 2);
    if(content.empty())
        return tags;

    size_t start = 0;
    size_t end = 0;
    bool in_quotes = false;
    char quote_char = '\0';

    while(end <= content.length()) {
        if(end < content.length() && (content[end] == '"' || content[end] == '\'')) {
            if(!in_quotes) {
                in_quotes = true;
                quote_char = content[end];
                start = end + 1;
            } else if(content[end] == quote_char) {
                in_quotes = false;
                tags.push_back(content.substr(start, end - start));
                while(end < content.length() && content[end] != ',')
                    ++end;
                start = end + 1;
            }
        } else if(!in_quotes && end < content.length() && content[end] == ',') {
            tags.push_back(content.substr(start, end - start));
            start = end + 1;
        }
        ++end;
    }

    if(start < content.length())
        tags.push_back(content.substr(start));

    return tags;
}

bool MetaStorage::contains(const std::string& id)
{
    if(!ok_) {
        MLOG_E("MetaStorage::contains({}): not ok", id);
        return false;
    }

    try {
        int exists = 0;
        sql << "SELECT 1 FROM orb_meta WHERE id = :id",
            soci::into(exists),
            soci::use(id);
        MLOG_D("MetaStorage::contains({}): {}", id, exists == 1 ? "found" : "not found");
        return exists == 1;
    } catch(const std::exception& e) {
        MLOG_E("MetaStorage::contains({}): exception: {}", id, e.what());
        return false;
    }
}

bool MetaStorage::store(const std::string& id, const InternalMetaObject& data)
{
    if(!ok_) {
        MLOG_E("MetaStorage::store({}): not ok, skipping", id);
        return false;
    }

    try {
        std::string tags = tags_to_pg_array(data.tags);
        std::string parent = data.parent.value_or("");
        std::string prev = data.prev.value_or("");
        std::string next = data.next.value_or("");
        int has_content_int = data.has_content ? 1 : 0;

        MLOG_D("MetaStorage::store({}): desc='{}', tags={}, parent='{}', has_content={}",
               id, data.description, tags, parent, has_content_int);

        sql << R"(
            INSERT INTO orb_meta (id, parent, prev, next, tags, description, has_content)
            VALUES (:id, NULLIF(:parent, ''), NULLIF(:prev, ''), NULLIF(:next, ''),
                    :tags::text[], :description, :has_content)
            ON CONFLICT (id)
            DO UPDATE SET
                parent      = NULLIF(:parent2, ''),
                prev        = NULLIF(:prev2, ''),
                next        = NULLIF(:next2, ''),
                tags        = :tags2::text[],
                description = :description2,
                has_content = :has_content2
        )",
            soci::use(id),
            soci::use(parent), soci::use(prev), soci::use(next),
            soci::use(tags), soci::use(data.description), soci::use(has_content_int),
            soci::use(parent), soci::use(prev), soci::use(next),
            soci::use(tags), soci::use(data.description), soci::use(has_content_int);

        MLOG_D("MetaStorage::store({}): success", id);
        return true;
    } catch(const std::exception& e) {
        MLOG_E("MetaStorage::store({}): exception: {}", id, e.what());
        return false;
    }
}

std::optional<InternalMetaObject> MetaStorage::load(const std::string& id)
{
    if(!ok_) {
        MLOG_E("MetaStorage::load({}): not ok", id);
        return std::nullopt;
    }

    try {
        InternalMetaObject obj;
        std::string parent, prev, next, tags_str;
        int has_content_int = 0;

        soci::indicator parent_ind = soci::i_null;
        soci::indicator prev_ind = soci::i_null;
        soci::indicator next_ind = soci::i_null;

        sql << R"(
            SELECT id, parent, prev, next, tags::text, description, has_content
            FROM orb_meta WHERE id = :id
        )",
            soci::into(obj.id),
            soci::into(parent, parent_ind),
            soci::into(prev, prev_ind),
            soci::into(next, next_ind),
            soci::into(tags_str),
            soci::into(obj.description),
            soci::into(has_content_int),
            soci::use(id);

        obj.has_content = has_content_int != 0;

        if(obj.id.empty())
            return std::nullopt;

        if(parent_ind == soci::i_ok && !parent.empty())
            obj.parent = parent;
        if(prev_ind == soci::i_ok && !prev.empty())
            obj.prev = prev;
        if(next_ind == soci::i_ok && !next.empty())
            obj.next = next;

        obj.tags = pg_array_to_tags(tags_str);

        MLOG_D("MetaStorage::load({}): found desc='{}' tags_count={}", id, obj.description, obj.tags.size());
        return obj;
    } catch(const std::exception& e) {
        MLOG_E("MetaStorage::load({}): exception: {}", id, e.what());
        return std::nullopt;
    }
}

std::vector<InternalMetaObject> MetaStorage::search_by_tags(const std::vector<std::string>& tags)
{
    std::vector<InternalMetaObject> results;
    if(!ok_ || tags.empty()) {
        MLOG_D("MetaStorage::search_by_tags: not ok or empty tags");
        return results;
    }

    try {
        // Build SQL: find meta objects whose tags array contains ALL specified tags
        // Use && (overlap) operator: tags @> ARRAY['tag1','tag2']::text[]
        std::string tags_arr = tags_to_pg_array(tags);
        std::string query = R"(
            SELECT id, parent, prev, next, tags::text, description, has_content
            FROM orb_meta
            WHERE tags @> :tags::text[]
        )";

        soci::rowset<soci::row> rs = (sql.prepare << query, soci::use(tags_arr));

        for(auto it = rs.begin(); it != rs.end(); ++it) {
            InternalMetaObject obj;
            const soci::row& row = *it;

            obj.id = row.get<std::string>("id", "");
            if(obj.id.empty())
                continue;

            if(row.get_indicator("parent") != soci::i_null)
                obj.parent = row.get<std::string>("parent", "");
            if(row.get_indicator("prev") != soci::i_null)
                obj.prev = row.get<std::string>("prev", "");
            if(row.get_indicator("next") != soci::i_null)
                obj.next = row.get<std::string>("next", "");

            obj.description = row.get<std::string>("description", "");
            obj.has_content = row.get<int>("has_content", 0) != 0;

            std::string tags_str = row.get<std::string>("tags", "{}");
            obj.tags = pg_array_to_tags(tags_str);

            results.push_back(std::move(obj));
        }

        MLOG_D("MetaStorage::search_by_tags: found {} results for {} tags",
               results.size(), tags.size());
    } catch(const std::exception& e) {
        MLOG_E("MetaStorage::search_by_tags: exception: {}", e.what());
    }

    return results;
}

size_t MetaStorage::count()
{
    if(!ok_) return 0;

    try {
        size_t cnt = 0;
        sql << "SELECT COUNT(*) FROM orb_meta", soci::into(cnt);
        return cnt;
    } catch(const std::exception&) {
        return 0;
    }
}

bool MetaStorage::ok()
{
    return ok_;
}