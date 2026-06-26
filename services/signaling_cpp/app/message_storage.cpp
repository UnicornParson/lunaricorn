#include "message_storage.h"

#include <sstream>
#include <iomanip>

namespace lunaricorn
{

MessageStorage::MessageStorage(const DbConfig& cfg)
{
    if(!cfg.valid()) 
    {
        throw std::invalid_argument("invalid db config");
    }

    try
    {
        std::ostringstream conn;
        conn<< "host=" << cfg.dbHost
            << " port=" << cfg.dbPort
            << " user=" << cfg.dbUser
            << " password=" << cfg.dbPassword
            << " dbname=" << cfg.dbDbname;
        sql.open(soci::postgresql,conn.str());
    }
    catch(const std::exception& e)
    {
        throw BrokenStorageError(e.what());
    }

}


std::string MessageStorage::json_to_string(
        const json::value& v
)
{
    return json::serialize(v);
}

long long MessageStorage::create_event(
        const EventData& event
)
{

    soci::indicator payload_ind =
        event.payload.is_null()
        ? soci::i_null
        : soci::i_ok;


    soci::indicator affected_ind =
        event.affected.is_null()
        ? soci::i_null
        : soci::i_ok;



    std::string payload =
        json_to_string(event.payload);


    std::string affected =
        json_to_string(event.affected);



    std::string owner =
        event.source
        ? *event.source
        : "ownerless";



    long long eid;



    soci::statement st =
        (sql.prepare <<
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
            :tags
        )
        RETURNING eid
        )",
        
        soci::into(eid),
        soci::use(event.event_type),
        soci::use(payload, payload_ind),
        soci::use(affected, affected_ind),
        soci::use(event.timestamp),
        soci::use(owner),
        soci::use(event.tags)
        );


    st.execute(true);


    return eid;

}






std::vector<std::string>
MessageStorage::get_unique_values(
        const std::string& field
)
{

    if(
        field!="type" &&
        field!="owner" &&
        field!="tags" &&
        field!="affected"
    )
        throw std::invalid_argument(
            "unsupported field"
        );



    std::vector<std::string> result;



    if(field=="tags")
    {

        soci::rowset<std::string> rows =
            (
                sql.prepare <<
                R"(
                SELECT DISTINCT unnest(tags)
                FROM signaling_events
                ORDER BY 1
                )"
            );


        for(auto& r: rows)
            result.push_back(r);

    }
    else if(field=="affected")
    {

        soci::rowset<std::string> rows =
        (
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

    }
    else
    {

        soci::rowset<std::string> rows =
        (
            sql.prepare <<
            ("SELECT DISTINCT "
             + field +
             " FROM signaling_events "
             "WHERE "
             + field +
             " IS NOT NULL "
             "ORDER BY 1")
        );


        for(auto& r: rows)
            result.push_back(r);

    }


    return result;
}








std::vector<EventDataExtended>
MessageStorage::find_events(
        double timestamp,
        const std::vector<std::string>& types,
        const std::vector<std::string>& sources,
        const std::vector<std::string>& affected,
        const std::vector<std::string>& tags,
        int limit
)
{


    std::stringstream q;


    q <<
    R"(
    SELECT
        eid,
        type,
        payload,
        affected,
        extract(epoch from ctime),
        owner,
        tags
    FROM signaling_events
    WHERE ctime >= to_timestamp(:ts)
    )";



    if(!types.empty())
        q<<" AND type IN (:types)";


    if(!sources.empty())
        q<<" AND owner IN (:owners)";


    if(!tags.empty())
        q<<" AND tags && :tags";


    q<<" ORDER BY ctime DESC ";


    if(limit>0)
        q<<" LIMIT "<<limit;




    soci::rowset<soci::row> rows =
    (
        sql.prepare <<
        q.str(),
        soci::use(timestamp),
        soci::use(types),
        soci::use(sources),
        soci::use(tags)
    );



    std::vector<EventDataExtended> result;


    for(auto& row: rows)
    {
        result.push_back(
            row_to_event(row)
        );
    }


    return result;
}










std::vector<EventDataExtended>
MessageStorage::find_events_by_type(
        const std::string& type
)
{

    soci::rowset<soci::row> rows =
    (
        sql.prepare <<
        R"(
        SELECT
            eid,
            type,
            payload,
            affected,
            extract(epoch from ctime),
            owner,
            tags

        FROM signaling_events

        WHERE type=:type

        ORDER BY ctime DESC
        )",

        soci::use(type)
    );



    std::vector<EventDataExtended> result;



    for(auto& row: rows)
    {
        result.push_back(
            row_to_event(row)
        );
    }


    return result;
}









EventDataExtended
MessageStorage::row_to_event(
        soci::row& row
)
{

    EventDataExtended e;



    e.eid =
        row.get<long long>(0);



    e.event_type =
        row.get<std::string>(1);



    std::string payload =
        row.get<std::string>(2);



    std::string affected =
        row.get<std::string>(3);



    e.payload =
        json::parse(payload);



    if(!affected.empty())
        e.affected =
            json::parse(affected);



    e.timestamp =
        row.get<double>(4);



    std::string owner =
        row.get<std::string>(5);



    if(owner!="ownerless")
        e.source = owner;



    e.tags =
        row.get<std::vector<std::string>>(6);



    return e;
}

} // namespace lunaricorn