
cli:

```rust
=== Signaling CLI ===
Connecting to 127.0.0.1:8080
Press Ctrl+C to exit
=====================
[2026-07-02T20:05:58.855Z] Connected!
signaling_api.cpp:bool lunaricorn::SignalingConnector::subscribe(const std::vector<std::__cxx11::basic_string<char> >&, const std::vector<std::__cxx11::basic_string<char> >&, const std::vector<std::__cxx11::basic_string<char> >&, const std::vector<std::__cxx11::basic_string<char> >&):596 [ DEBUG ] send subscribe seq=0, types=0
signaling_api.cpp:bool lunaricorn::SignalingConnector::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):483 [ DEBUG ] /opt/cli/apilib/signaling_api.cpp.send_message.483 MessageHeader: magic=0x12345678 version=1 type=5 data_type=1 flags=0 seq=0 data_len=2 crc=0x00000000 dump=78 56 34 12 01 05 01 00 00 00 00 00 00 00 00 00 02 00 00 00 00 00 00 00
signaling_api.cpp:bool lunaricorn::SignalingConnector::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):496 [ DEBUG ] try to send 24b
signaling_api.cpp:void lunaricorn::SignalingConnector::send_client_hb():169 [ DEBUG ] /opt/cli/apilib/signaling_api.cpp.send_client_hb.169 MessageHeader: magic=0x12345678 version=1 type=1 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
signaling_api.cpp:bool lunaricorn::SignalingConnector::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):483 [ DEBUG ] /opt/cli/apilib/signaling_api.cpp.send_message.483 MessageHeader: magic=0x12345678 version=1 type=1 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
signaling_api.cpp:bool lunaricorn::SignalingConnector::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):496 [ DEBUG ] try to send 24b
signaling_api.cpp:void lunaricorn::SignalingConnector::on_message(const lunaricorn::internal::IncomingMessage&):320 [ DEBUG ] /opt/cli/apilib/signaling_api.cpp.on_message.320 MessageHeader: magic=0x12345678 version=1 type=1 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[[2026-07-02T20:06:08.858Z] RESP | type=RESP data_type=JSON seq=0 data_len=0
2026-07-02T20:06:08.858Z] Subscription sent (all event types)
[2026-07-02T20:06:08.859Z] [TestSender] Connected to 127.0.0.1:8080
[2026-07-02T20:06:08.859Z] [TestSender] Test sender started
[2026-07-02T20:06:08.859Z] Test sender started

[2026-07-02T20:06:08.859Z] [TestSender] Runner thread started
signaling.cpp:boost::json::object lunaricorn::internal::SignalingEvent::toDict() const:335 [ DEBUG ] SignalingEvent::toDict serialized type='orb.system' source='risk_engine' tags=3 payload_keys=4 timestamp=1783022768.859132
signaling_api.cpp:bool lunaricorn::SignalingConnector::push(const lunaricorn::internal::SignalingEvent&):525 [ DEBUG ] /opt/cli/apilib/signaling_api.cpp.push.525 MessageHeader: magic=0x12345678 version=1 type=3 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 03 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
signaling_api.cpp:bool lunaricorn::SignalingConnector::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):483 [ DEBUG ] /opt/cli/apilib/signaling_api.cpp.send_message.483 MessageHeader: magic=0x12345678 version=1 type=3 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 03 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
signaling_api.cpp:bool lunaricorn::SignalingConnector::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):496 [ DEBUG ] try to send 233b
signaling_api.cpp:void lunaricorn::SignalingConnector::on_message(const lunaricorn::internal::IncomingMessage&):320 [ DEBUG ] /opt/cli/apilib/signaling_api.cpp.on_message.320 MessageHeader: magic=0x12345678 version=1 type=2 data_type=1 flags=0 seq=0 data_len=37 crc=0xeacd3f80 dump=78 56 34 12 01 02 01 00 00 00 00 00 00 00 00 00 25 00 00 00 80 3f cd ea
signaling_api.cpp:void lunaricorn::SignalingConnector::on_server_request(const lunaricorn::internal::IncomingMessage&):389 [ DEBUG ] on server response (not matched via pending)
signaling_api.cpp:void lunaricorn::SignalingConnector::on_message(const lunaricorn::internal::IncomingMessage&):320 [ DEBUG ] /opt/cli/apilib/signaling_api.cpp.on_message.320 MessageHeader: magic=0x12345678 version=1 type=2 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 02 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
signaling_api.cpp:void lunaricorn::SignalingConnector::on_server_request(const lunaricorn::internal::IncomingMessage&):389 [ DEBUG ] on server response (not matched via pending)
signaling_api.cpp:void lunaricorn::SignalingConnector::on_message(const lunaricorn::internal::IncomingMessage&):320 [ DEBUG ] /opt/cli/apilib/signaling_api.cpp.on_message.320 MessageHeader: magic=0x12345678 version=1 type=1 data_type=1 flags=0 seq=0 data_len=15 crc=0x1d13eddd dump=78 56 34 12 01 01 01 00 00 00 00 00 00 00 00 00 0f 00 00 00 dd ed 13 1d
signaling_api.cpp:void lunaricorn::SignalingConnector::on_server_request(const lunaricorn::internal::IncomingMessage&):384 [ DEBUG ] on server hb
[2026-07-02T20:06:08.869Z] DISCONNECT: read error _0
signaling_api.cpp:void lunaricorn::SignalingConnector::runner(std::stop_token, std::shared_ptr<lunaricorn::internal::ThreadState>):477 [ ERROR ] thread emergency exit @6505689171550705518

[2026-07-02T20:06:09.359Z] Exiting...
thread_control.cpp:lunaricorn::internal::OrphanThreadManager::OrphanThreadManager():84 [ DEBUG ] OrphanThreadManager started
signaling_api.cpp:void lunaricorn::SignalingConnector::runner(std::stop_token, std::shared_ptr<lunaricorn::internal::ThreadState>):477 [ ERROR ] thread emergency exit @17097733130461563929
signaling_api.cpp:bool lunaricorn::SignalingConnector::push(const lunaricorn::internal::SignalingEvent&):529 [ DEBUG ] pushed event with seq 0
[2026-07-02T20:06:09.860Z] [TestSender] Pushed event seq=1 type=orb.system source=risk_engine payload={"test_id":2536,"seq":1,"random_value":5.068E1,"event_type_idx":6}
thread_control.cpp:void lunaricorn::internal::OrphanThreadManager::monitorLoop(std::stop_token):30 [ DEBUG ] Orphan thread 'SignalingConnector::runner' finished in 1000 ms
[2026-07-02T20:06:10.860Z] [TestSender] Runner thread exiting
[2026-07-02T20:06:10.860Z] [TestSender] Stopped. sent=1 errors=0
thread_control.cpp:lunaricorn::internal::OrphanThreadManager::~OrphanThreadManager():94 [ DEBUG ] OrphanThreadManager stopped
```

server:

```rust
raw_endpoint.cpp:void lunaricorn::RawEndpoint::acceptLoop():92 [ DEBUG ] start accept loop
raw_endpoint.cpp:void lunaricorn::RawEndpoint::acceptLoop():104 [ DEBUG ] acceptLoop: waiting for incoming connection...
raw_endpoint.cpp:void lunaricorn::RawEndpoint::acceptLoop():110 [ DEBUG ] acceptLoop: new connection from 127.0.0.1:127.0.0.1, port 8080
raw_endpoint.cpp:void lunaricorn::RawEndpoint::acceptLoop():123 [ DEBUG ] acceptLoop: assigned client id=1, total clients=1
raw_endpoint.cpp:void lunaricorn::RawEndpoint::acceptLoop():133 [ DEBUG ] acceptLoop: client 1 added to _clients map, total connected: 1
raw_endpoint.cpp:void lunaricorn::RawEndpoint::acceptLoop():136 [ DEBUG ] acceptLoop: new client 1 accepted and registered
raw_endpoint.cpp:void lunaricorn::RawEndpoint::send_hb():173 [ DEBUG ] send hb to 1
raw_endpoint.cpp:void lunaricorn::RawEndpoint::sendHeartbeat(uint64_t):416 [ DEBUG ] sendHeartbeat[1]: sending heartbeat
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):548 [ DEBUG ] /opt/app/raw_endpoint_client.cpp.send_message.548 MessageHeader: magic=0x12345678 version=1 type=1 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):568 [ DEBUG ] try to send 24b
raw_endpoint.cpp:void lunaricorn::RawEndpoint::acceptLoop():104 [ DEBUG ] acceptLoop: waiting for incoming connection...
raw_endpoint.cpp:void lunaricorn::RawEndpoint::acceptLoop():110 [ DEBUG ] acceptLoop: new connection from 127.0.0.1:127.0.0.1, port 8080
raw_endpoint.cpp:void lunaricorn::RawEndpoint::acceptLoop():123 [ DEBUG ] acceptLoop: assigned client id=2, total clients=2
raw_endpoint.cpp:void lunaricorn::RawEndpoint::acceptLoop():133 [ DEBUG ] acceptLoop: client 2 added to _clients map, total connected: 2
raw_endpoint.cpp:void lunaricorn::RawEndpoint::acceptLoop():136 [ DEBUG ] acceptLoop: new client 2 accepted and registered
raw_endpoint_client.cpp:void lunaricorn::RE_Client::on_message(const lunaricorn::internal::IncomingMessage&):197 [ DEBUG ] /opt/app/raw_endpoint_client.cpp.on_message.197 MessageHeader: magic=0x12345678 version=1 type=5 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 05 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
signaling_engine.cpp:void lunaricorn::SignalingEngine::subscribe(uint64_t, const std::vector<std::__cxx11::basic_string<char> >&, const std::vector<std::__cxx11::basic_string<char> >&, const std::vector<std::__cxx11::basic_string<char> >&, const std::vector<std::__cxx11::basic_string<char> >&):90 Client 1 subscribed to events
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):548 [ DEBUG ] /opt/app/raw_endpoint_client.cpp.send_message.548 MessageHeader: magic=0x12345678 version=1 type=2 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 02 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):568 [ DEBUG ] try to send 61b
raw_endpoint.cpp:void lunaricorn::RawEndpoint::on_client_message(uint64_t, const lunaricorn::internal::IncomingMessage&):263 [ DEBUG ] on_client_message[1]: received msg type=5 data_type=1 seq=0 data_len=0
raw_endpoint.cpp:void lunaricorn::RawEndpoint::processSubscription(uint64_t, const lunaricorn::internal::IncomingMessage&):327 [ DEBUG ] processSubscription[1]: received subscription request, seq=0, data_len=0
raw_endpoint.cpp:void lunaricorn::RawEndpoint::processSubscription(uint64_t, const lunaricorn::internal::IncomingMessage&):338 [ DEBUG ] processSubscription[1]: sending acknowledgment
raw_endpoint.cpp:void lunaricorn::RawEndpoint::sendResponse(uint64_t, uint64_t, bool, const boost::json::object&):428 [ DEBUG ] sendResponse[1]: sending response, seq=0, success=true, data_obj_size=0
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):548 [ DEBUG ] /opt/app/raw_endpoint_client.cpp.send_message.548 MessageHeader: magic=0x12345678 version=1 type=2 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 02 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):568 [ DEBUG ] try to send 24b
raw_endpoint.cpp:void lunaricorn::RawEndpoint::sendResponse(uint64_t, uint64_t, bool, const boost::json::object&):456 [ DEBUG ] sendResponse[1]: response sent successfully
raw_endpoint.cpp:void lunaricorn::RawEndpoint::processSubscription(uint64_t, const lunaricorn::internal::IncomingMessage&):340 [ DEBUG ] processSubscription[1]: subscription acknowledged
raw_endpoint_client.cpp:void lunaricorn::RE_Client::on_message(const lunaricorn::internal::IncomingMessage&):197 [ DEBUG ] /opt/app/raw_endpoint_client.cpp.on_message.197 MessageHeader: magic=0x12345678 version=1 type=1 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):548 [ DEBUG ] /opt/app/raw_endpoint_client.cpp.send_message.548 MessageHeader: magic=0x12345678 version=1 type=1 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):568 [ DEBUG ] try to send 39b
raw_endpoint.cpp:void lunaricorn::RawEndpoint::on_client_message(uint64_t, const lunaricorn::internal::IncomingMessage&):263 [ DEBUG ] on_client_message[1]: received msg type=1 data_type=1 seq=0 data_len=0
raw_endpoint.cpp:void lunaricorn::RawEndpoint::processHeartbeat(uint64_t, const lunaricorn::internal::IncomingMessage&):313 [ DEBUG ] processHeartbeat[1]: received heartbeat, data_len=0
raw_endpoint.cpp:void lunaricorn::RawEndpoint::processHeartbeat(uint64_t, const lunaricorn::internal::IncomingMessage&):319 [ DEBUG ] processHeartbeat[1]: heartbeat updated, total connected: 2
raw_endpoint.cpp:void lunaricorn::RawEndpoint::handleClients():231 [ ERROR ] receiveBytes failed for client#1: error code=-1
raw_endpoint.cpp:void lunaricorn::RawEndpoint::on_client_closed(uint64_t):292 [ DEBUG ] on_client_closed[1]: client closing, current connected: 2
raw_endpoint.cpp:void lunaricorn::RawEndpoint::on_client_closed(uint64_t):304 [ DEBUG ] on_client_closed[1]: client disconnected. session_duration=0s, total connected: 1
raw_endpoint_client.cpp:void lunaricorn::RE_Client::on_message(const lunaricorn::internal::IncomingMessage&):197 [ DEBUG ] /opt/app/raw_endpoint_client.cpp.on_message.197 MessageHeader: magic=0x12345678 version=1 type=3 data_type=1 flags=0 seq=0 data_len=209 crc=0x4f7ea21c dump=78 56 34 12 01 03 01 00 00 00 00 00 00 00 00 00 d1 00 00 00 1c a2 7e 4f
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):548 [ DEBUG ] /opt/app/raw_endpoint_client.cpp.send_message.548 MessageHeader: magic=0x12345678 version=1 type=2 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 02 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):568 [ DEBUG ] try to send 55b
raw_endpoint.cpp:void lunaricorn::RawEndpoint::on_client_message(uint64_t, const lunaricorn::internal::IncomingMessage&):263 [ DEBUG ] on_client_message[2]: received msg type=3 data_type=1 seq=0 data_len=209
raw_endpoint.cpp:void lunaricorn::RawEndpoint::processPushRequest(uint64_t, const lunaricorn::internal::IncomingMessage&):345 [ DEBUG ] processPushRequest[2]: received push request, seq=0, data_len=209
raw_endpoint.cpp:void lunaricorn::RawEndpoint::processPushRequest(uint64_t, const lunaricorn::internal::IncomingMessage&):350 [ DEBUG ] processPushRequest[2]: push payload size=6
raw_endpoint.cpp:void lunaricorn::RawEndpoint::processPushRequest(uint64_t, const lunaricorn::internal::IncomingMessage&):358 [ DEBUG ] processPushRequest[2]: sending acknowledgment
raw_endpoint.cpp:void lunaricorn::RawEndpoint::sendResponse(uint64_t, uint64_t, bool, const boost::json::object&):428 [ DEBUG ] sendResponse[2]: sending response, seq=0, success=true, data_obj_size=0
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):548 [ DEBUG ] /opt/app/raw_endpoint_client.cpp.send_message.548 MessageHeader: magic=0x12345678 version=1 type=2 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000 dump=78 56 34 12 01 02 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):568 [ DEBUG ] try to send 24b
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(lunaricorn::internal::MessageHeader&, const boost::json::object&):582 [ ERROR ] send_bytes exception: I/O error: Broken pipe
raw_endpoint.cpp:void lunaricorn::RawEndpoint::sendResponse(uint64_t, uint64_t, bool, const boost::json::object&):456 [ DEBUG ] sendResponse[2]: response sent successfully
raw_endpoint.cpp:void lunaricorn::RawEndpoint::on_client_closed(uint64_t):292 [ DEBUG ] on_client_closed[2]: client closing, current connected: 1
raw_endpoint.cpp:void lunaricorn::RawEndpoint::on_client_closed(uint64_t):304 [ DEBUG ] on_client_closed[2]: client disconnected. session_duration=1s, total connected: 0

```
