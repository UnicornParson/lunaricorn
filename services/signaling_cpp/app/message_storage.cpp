#include "message_storage.h"

#include <sstream>
#include <iomanip>

namespace lunaricorn
{

namespace
{

constexpr const char* EVENT_SELECT_COLUMNS = R"(
    eid,
    type,
    payload,
    affected,
    extract(epoch from ctime),
    owner,
    tags::text
)";

} // namespace

MessageStorage::MessageStorage(const DbConfig& cfg)
{
    if(!cfg.valid()) {
        throw std::invalid_argument("invalid db config");
    }

    try {
        std::ostringstream conn;
        conn<< "host=" << cfg.dbHost
            << " port=" << cfg.dbPort
            << " user=" << cfg.dbUser
            << " password=" << cfg.dbPassword
            << " dbname=" << cfg.dbDbname;
        sql.open(soci::postgresql,conn.str());
    } catch(const std::exception& e) {
        throw BrokenStorageError(e.what());
    }
}

std::string MessageStorage::json_to_string(const json::value& v)
{
    return json::serialize(v);
}

std::string MessageStorage::tags_to_pg_array(const std::vector<std::string>& tags)
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

std::vector<std::string> MessageStorage::pg_array_to_tags(const std::string& tags_str)
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

long long MessageStorage::create_event(const StoredEventData& event)
{
    soci::indicator payload_ind =
        event.payload.is_null() ? soci::i_null : soci::i_ok;
    soci::indicator affected_ind =
        event.affected.is_null() ? soci::i_null : soci::i_ok;

    std::string payload = json_to_string(event.payload);
    std::string affected = json_to_string(event.affected);
    std::string owner = event.source ? *event.source : "ownerless";
    std::string tags = tags_to_pg_array(event.tags);

    long long eid;

    soci::statement st = (sql.prepare <<
        R"(
        INSERT INTO signaling_events
        (
            type,
            payload,
            affected,
            ctime,
            owner,
            tags
        )
        VALUES
        (
            :type,
            :payload::jsonb,
            :affected::jsonb,
            to_timestamp(:ctime),
            :owner,
            :tags::text[]
        )
        RETURNING eid
        )",
        soci::into(eid),
        soci::use(event.event_type),
        soci::use(payload, payload_ind),
        soci::use(affected, affected_ind),
        soci::use(event.timestamp),
        soci::use(owner),
        soci::use(tags)
    );

    st.execute(true);
    return eid;
}

std::vector<std::string> MessageStorage::get_unique_values(const std::string& field)
{
    if(field!="type" && field!="owner" && field!="tags" && field!="affected")
        throw std::invalid_argument("unsupported field");

    std::vector<std::string> result;

    if(field=="tags") {
        soci::rowset<std::string> rows = (
            sql.prepare <<
            R"(
            SELECT DISTINCT unnest(tags)
            FROM signaling_events
            ORDER BY 1
            )"
        );
        for(auto& r: rows)
            result.push_back(r);
    } else if(field=="affected") {
        soci::rowset<std::string> rows = (
            sql.prepare <<
            R"(
            SELECT DISTINCT
            jsonb_array_elements_text(affected)
            FROM signaling_events
            ORDER BY 1
            )"
        );
        for(auto& r: rows)
            result.push_back(r);
    } else {
        soci::rowset<std::string> rows = (
            sql.prepare <<
            ("SELECT DISTINCT " + field +
             " FROM signaling_events "
             "WHERE " + field +
             " IS NOT NULL "
             "ORDER BY 1")
        );
        for(auto& r: rows)
            result.push_back(r);
    }

    return result;
}

std::vector<StoredEventDataExtended> MessageStorage::find_events(
    double timestamp,
    const std::vector<std::string>& types,
    const std::vector<std::string>& sources,
    const std::vector<std::string>& affected,
    const std::vector<std::string>& tags,
    int limit
)
{
    (void)affected;

    std::stringstream q;
    q <<
    "SELECT "
    << EVENT_SELECT_COLUMNS <<
    R"(
    FROM signaling_events
    WHERE ctime >= to_timestamp(:ts)
    )";

    if(!types.empty())
        q<<" AND type IN (:types)";
    if(!sources.empty())
        q<<" AND owner IN (:owners)";
    if(!tags.empty())
        q<<" AND tags && :tags::text[]";

    q<<" ORDER BY ctime DESC ";
    if(limit>0)
        q<<" LIMIT "<<limit;

    const std::string query = q.str();
    const std::string tags_pg = tags_to_pg_array(tags);
    const int filter_mask =
        (types.empty() ? 0 : 1) |
        (sources.empty() ? 0 : 2) |
        (tags.empty() ? 0 : 4);

    std::vector<StoredEventDataExtended> result;

    switch(filter_mask) {
    case 0: {
        soci::rowset<soci::row> rows = (sql.prepare << query, soci::use(timestamp));
        for(auto& row: rows)
            result.push_back(row_to_event(row));
        break;
    }
    case 1: {
        soci::rowset<soci::row> rows = (
            sql.prepare << query, soci::use(timestamp), soci::use(types));
        for(auto& row: rows)
            result.push_back(row_to_event(row));
        break;
    }
    case 2: {
        soci::rowset<soci::row> rows = (
            sql.prepare << query, soci::use(timestamp), soci::use(sources));
        for(auto& row: rows)
            result.push_back(row_to_event(row));
        break;
    }
    case 3: {
        soci::rowset<soci::row> rows = (
            sql.prepare << query, soci::use(timestamp), soci::use(types), soci::use(sources));
        for(auto& row: rows)
            result.push_back(row_to_event(row));
        break;
    }
    case 4: {
        soci::rowset<soci::row> rows = (
            sql.prepare << query, soci::use(timestamp), soci::use(tags_pg));
        for(auto& row: rows)
            result.push_back(row_to_event(row));
        break;
    }
    case 5: {
        soci::rowset<soci::row> rows = (
            sql.prepare << query, soci::use(timestamp), soci::use(types), soci::use(tags_pg));
        for(auto& row: rows)
            result.push_back(row_to_event(row));
        break;
    }
    case 6: {
        soci::rowset<soci::row> rows = (
            sql.prepare << query, soci::use(timestamp), soci::use(sources), soci::use(tags_pg));
        for(auto& row: rows)
            result.push_back(row_to_event(row));
        break;
    }
    case 7: {
        soci::rowset<soci::row> rows = (
            sql.prepare << query,
            soci::use(timestamp),
            soci::use(types),
            soci::use(sources),
            soci::use(tags_pg));
        for(auto& row: rows)
            result.push_back(row_to_event(row));
        break;
    }
    }

    return result;
}

std::vector<StoredEventDataExtended> MessageStorage::find_events_by_type(const std::string& type)
{
    soci::rowset<soci::row> rows = (
        sql.prepare <<
        "SELECT "
        << EVENT_SELECT_COLUMNS <<
        R"(
        FROM signaling_events
        WHERE type=:type
        ORDER BY ctime DESC
        )",
        soci::use(type)
    );

    std::vector<StoredEventDataExtended> result;
    for(auto& row: rows)
        result.push_back(row_to_event(row));

    return result;
}

StoredEventDataExtended MessageStorage::row_to_event(soci::row& row)
{
    StoredEventDataExtended e;

    e.eid = row.get<long long>(0);
    e.event_type = row.get<std::string>(1);

    std::string payload = row.get<std::string>(2);
    std::string affected = row.get<std::string>(3);

    e.payload = json::parse(payload);
    if(!affected.empty())
        e.affected = json::parse(affected);

    e.timestamp = row.get<double>(4);

    std::string owner = row.get<std::string>(5);
    if(owner!="ownerless")
        e.source = owner;

    e.tags = pg_array_to_tags(row.get<std::string>(6));
    return e;
}

} // namespace lunaricorn
