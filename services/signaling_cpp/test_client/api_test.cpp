#include <boost/test/unit_test.hpp>
#include <boost/crc.hpp>
#include <span>
#include <vector>
#include <algorithm>
#include <iostream>
#include <boost/json.hpp>
#include <cstring>
#include <format>
#include <proto/signaling.h>
#include <lunaricorn.h>
using namespace lunaricorn::internal;

// Фикстура для тестов
struct SignalingProtoFixture {
    SignalingProto proto;
    MessageHeader dummyHeader;

    SignalingProtoFixture() {
        // make log stub mode
        lunaricorn::MLog::is_stub = true;

        dummyHeader.magic = HeaderMagic;
        dummyHeader.version = PROTOCOL_VERSION;
        dummyHeader.type = MT_PubReq;
        dummyHeader.flags = 0;
        dummyHeader.data_len = 0;
        dummyHeader.crc = 0;
        dummyHeader.data_type = CT_Raw;  // будет заменён в serializeJson
    }

    // Вспомогательная функция: создаёт корректный буфер из объекта JSON
    std::vector<uint8_t> createValidBuffer(const boost::json::object& data, MessageHeader& outHeader) {
        std::vector<uint8_t> buf;
        size_t sz = proto.serializeJson(dummyHeader, buf, data);
        // Извлечём заголовок из начала буфера
        std::memcpy(&outHeader, buf.data(), sizeof(MessageHeader));
        return buf;
    }
};

BOOST_FIXTURE_TEST_SUITE(SignalingProtoTests, SignalingProtoFixture)

// ======================== serializeJson ========================

BOOST_AUTO_TEST_CASE(SerializeJson_ValidData_ReturnsCorrectSize) {
    boost::json::object data = {{"key", "value"}, {"number", 42}};
    std::vector<uint8_t> buffer;
    size_t result = proto.serializeJson(dummyHeader, buffer, data);
    
    // Проверяем возвращаемый размер
    std::string json = boost::json::serialize(data);
    size_t expectedSize = sizeof(MessageHeader) + json.size();
    BOOST_CHECK_EQUAL(result, expectedSize);
    BOOST_CHECK_EQUAL(buffer.size(), expectedSize);
}

BOOST_AUTO_TEST_CASE(SerializeJson_OriginalHeaderNotModified) {
    // Так как serializeJson копирует заголовок и меняет копию, оригинал должен остаться неизменным
    MessageHeader original = dummyHeader;
    original.data_type = CT_Raw;
    original.crc = 0x12345678;
    original.data_len = 999;
    
    boost::json::object data = {{"test", true}};
    std::vector<uint8_t> buffer;
    proto.serializeJson(original, buffer, data);
    
    // Проверяем, что оригинал не изменился
    BOOST_CHECK_EQUAL(original.data_type, CT_Raw);
    BOOST_CHECK_EQUAL(original.crc, 0x12345678);
    BOOST_CHECK_EQUAL(original.data_len, 999);
    // Остальные поля тоже должны быть нетронуты
    BOOST_CHECK_EQUAL(original.magic, HeaderMagic);
    BOOST_CHECK_EQUAL(original.version, PROTOCOL_VERSION);
}

BOOST_AUTO_TEST_CASE(SerializeJson_EmptyObject_Works) {
    boost::json::object data = {};
    std::vector<uint8_t> buffer;
    BOOST_CHECK_NO_THROW(proto.serializeJson(dummyHeader, buffer, data));
    BOOST_CHECK_EQUAL(buffer.size(), sizeof(MessageHeader));
}

BOOST_AUTO_TEST_CASE(SerializeJson_DataTooLarge_ThrowsLengthError) {
    // Создаём JSON, который после сериализации превышает MAX_DATA_LEN (128 МБ)
    // Чтобы не выделять 128 МБ памяти, используем длинную строку
    std::string longStr(MAX_DATA_LEN + 1, 'x');
    boost::json::object data = {{"big", longStr}};
    std::vector<uint8_t> buffer;
    BOOST_CHECK_THROW(proto.serializeJson(dummyHeader, buffer, data), std::length_error);
    // Буфер должен остаться пустым (или содержать только то, что успело записаться до исключения)
    // Но по логике исключение выбрасывается до вставки, поэтому buffer.size() == 0
    BOOST_CHECK_EQUAL(buffer.size(), 0);
}

BOOST_AUTO_TEST_CASE(SerializeJson_SetsCorrectHeaderFields) {
    boost::json::object data = {{"foo", "bar"}};
    std::vector<uint8_t> buffer;
    MessageHeader before = dummyHeader;
    before.data_type = CT_Raw; // будет перезаписан внутри на CT_Json
    
    proto.serializeJson(before, buffer, data);
    
    // Извлекаем заголовок из буфера
    MessageHeader written;
    std::memcpy(&written, buffer.data(), sizeof(MessageHeader));
    
    // Поля магии, версии, типа сообщения должны совпадать с переданными (кроме data_type)
    BOOST_CHECK_EQUAL(written.magic, HeaderMagic);
    BOOST_CHECK_EQUAL(written.version, PROTOCOL_VERSION);
    BOOST_CHECK_EQUAL(written.type, dummyHeader.type); // MT_PubReq
    BOOST_CHECK_EQUAL(written.data_type, CT_Json);     // принудительно установлен
    BOOST_CHECK_EQUAL(written.flags, dummyHeader.flags);
    BOOST_CHECK_EQUAL(written.data_len, buffer.size() - sizeof(MessageHeader));
    // CRC проверяется в deserializeJson, тут только наличие
    BOOST_CHECK_NE(written.crc, 0);
}

// ======================== deserializeJson ========================

BOOST_AUTO_TEST_CASE(DeserializeJson_ValidBuffer_Success) {
    boost::json::object originalData = {{"msg", "hello"}, {"id", 123}};
    MessageHeader expectedHeader;
    std::vector<uint8_t> buffer = createValidBuffer(originalData, expectedHeader);
    
    IncomingMessage msg;
    bool result = proto.deserializeJson(buffer, msg);
    
    BOOST_CHECK(result);
    BOOST_CHECK(msg.isValid);
    BOOST_CHECK(msg.errorReason.empty());
    // Сравниваем заголовок
    BOOST_CHECK_EQUAL(msg.header.magic, expectedHeader.magic);
    BOOST_CHECK_EQUAL(msg.header.version, expectedHeader.version);
    BOOST_CHECK_EQUAL(msg.header.type, expectedHeader.type);
    BOOST_CHECK_EQUAL(msg.header.data_type, CT_Json);
    BOOST_CHECK_EQUAL(msg.header.data_len, expectedHeader.data_len);
    BOOST_CHECK_EQUAL(msg.header.crc, expectedHeader.crc);
    // Сравниваем JSON
    BOOST_CHECK_EQUAL(boost::json::serialize(msg.data), boost::json::serialize(originalData));
}

BOOST_AUTO_TEST_CASE(DeserializeJson_BufferTooSmall_ReturnsFalse) {
    std::vector<uint8_t> smallBuf(sizeof(MessageHeader) - 1, 0xAB);
    IncomingMessage msg;
    bool result = proto.deserializeJson(smallBuf, msg);
    
    BOOST_CHECK(!result);
    BOOST_CHECK(!msg.isValid);
    BOOST_CHECK(!msg.errorReason.empty());
    BOOST_CHECK(msg.errorReason.find("Buffer too small") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(DeserializeJson_WrongMagic_ReturnsFalse) {
    boost::json::object data = {{"a", 1}};
    MessageHeader dummy;
    std::vector<uint8_t> buffer = createValidBuffer(data, dummy);
    // Портим magic
    uint32_t* magicPtr = reinterpret_cast<uint32_t*>(buffer.data());
    *magicPtr = 0xDEADBEEF;
    
    IncomingMessage msg;
    bool result = proto.deserializeJson(buffer, msg);
    
    BOOST_CHECK(!result);
    BOOST_CHECK(!msg.isValid);
    BOOST_CHECK(msg.errorReason.find("Magic mismatch") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(DeserializeJson_WrongVersion_ReturnsFalse) {
    boost::json::object data = {{"x", "y"}};
    MessageHeader dummy;
    std::vector<uint8_t> buffer = createValidBuffer(data, dummy);
    // Меняем версию (смещение 4 байта от начала: после magic)
    buffer[4] = PROTOCOL_VERSION + 1;
    
    IncomingMessage msg;
    bool result = proto.deserializeJson(buffer, msg);
    
    BOOST_CHECK(!result);
    BOOST_CHECK(msg.errorReason.find("Version mismatch") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(DeserializeJson_NonJsonContentType_ReturnsFalse) {
    boost::json::object data = {{"any", "value"}};
    MessageHeader dummy;
    std::vector<uint8_t> buffer = createValidBuffer(data, dummy);
    // Меняем data_type на CT_Raw (0) в заголовке (смещение 9 байт? Лучше через структуру)
    MessageHeader* hdr = reinterpret_cast<MessageHeader*>(buffer.data());
    hdr->data_type = CT_Raw;
    
    IncomingMessage msg;
    bool result = proto.deserializeJson(buffer, msg);
    
    BOOST_CHECK(!result);
    BOOST_CHECK(msg.errorReason.find("Content type mismatch") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(DeserializeJson_DataLenExceedsMax_ReturnsFalse) {
    boost::json::object data = {{"small", "data"}};
    MessageHeader dummy;
    std::vector<uint8_t> buffer = createValidBuffer(data, dummy);
    // Устанавливаем data_len больше MAX_DATA_LEN
    MessageHeader* hdr = reinterpret_cast<MessageHeader*>(buffer.data());
    hdr->data_len = MAX_DATA_LEN + 1;
    
    IncomingMessage msg;
    bool result = proto.deserializeJson(buffer, msg);
    
    BOOST_CHECK(!result);
    BOOST_CHECK(msg.errorReason.find("exceeds maximum allowed") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(DeserializeJson_IncompleteData_ReturnsFalse) {
    boost::json::object data = {{"payload", "this is a test"}};
    MessageHeader dummy;
    std::vector<uint8_t> buffer = createValidBuffer(data, dummy);
    // Обрезаем буфер, удаляя часть JSON данных
    buffer.resize(buffer.size() - 5);
    // data_len в заголовке остаётся старым, но буфер стал короче
    
    IncomingMessage msg;
    bool result = proto.deserializeJson(buffer, msg);
    
    BOOST_CHECK(!result);
    BOOST_CHECK(msg.errorReason.find("Incomplete data") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(DeserializeJson_CRC32Mismatch_ReturnsFalse) {
    boost::json::object data = {{"key", "value"}};
    MessageHeader dummy;
    std::vector<uint8_t> buffer = createValidBuffer(data, dummy);
    // Изменяем один байт в данных JSON
    buffer[sizeof(MessageHeader)] ^= 0xFF;
    // Заголовок остался с оригинальной CRC
    
    IncomingMessage msg;
    bool result = proto.deserializeJson(buffer, msg);
    
    BOOST_CHECK(!result);
    BOOST_CHECK(msg.errorReason.find("CRC32 mismatch") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(DeserializeJson_InvalidJson_ReturnsFalse) {
    // Создаём валидный буфер, затем портим JSON строку
    boost::json::object data = {{"valid", true}};
    MessageHeader dummy;
    std::vector<uint8_t> buffer = createValidBuffer(data, dummy);
    // Вставляем некорректный JSON (незакрытую строку)
    char* jsonStart = reinterpret_cast<char*>(buffer.data() + sizeof(MessageHeader));
    std::strcpy(jsonStart, "{ \"bad\": \"broken");
    // Пересчитывать CRC не будем — должен быть пойман либо парсер, либо CRC (но CRC скорее всего тоже не сойдётся)
    // Чтобы гарантированно пройти CRC, нужно пересчитать, но для теста ошибки парсинга проще просто подменить после CRC.
    // Лучше сгенерировать новый буфер с корректным CRC, но испорченным JSON:
    MessageHeader hdr2 = dummy;
    std::string malformedJson = "{ \"unclosed\": \"text";
    hdr2.data_len = malformedJson.size();
    boost::crc_32_type crc;
    crc.process_bytes(malformedJson.data(), malformedJson.size());
    hdr2.crc = crc.checksum();
    hdr2.data_type = CT_Json;
    
    std::vector<uint8_t> badBuffer;
    badBuffer.resize(sizeof(hdr2) + malformedJson.size());
    std::memcpy(badBuffer.data(), &hdr2, sizeof(hdr2));
    std::memcpy(badBuffer.data() + sizeof(hdr2), malformedJson.data(), malformedJson.size());
    
    IncomingMessage msg;
    bool result = proto.deserializeJson(badBuffer, msg);
    
    BOOST_CHECK(!result);
    BOOST_CHECK(!msg.isValid);
    BOOST_CHECK(msg.errorReason.find("JSON parse error") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(DeserializeJson_JsonNotObject_ReturnsFalse) {
    // Буфер с JSON массивом вместо объекта
    std::string jsonArray = "[1,2,3]";
    MessageHeader hdr;
    hdr.magic = HeaderMagic;
    hdr.version = PROTOCOL_VERSION;
    hdr.data_type = CT_Json;
    hdr.data_len = jsonArray.size();
    boost::crc_32_type crc;
    crc.process_bytes(jsonArray.data(), jsonArray.size());
    hdr.crc = crc.checksum();
    hdr.flags = 0;
    hdr.type = MT_PubReq;
    
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(hdr) + jsonArray.size());
    std::memcpy(buffer.data(), &hdr, sizeof(hdr));
    std::memcpy(buffer.data() + sizeof(hdr), jsonArray.data(), jsonArray.size());
    
    IncomingMessage msg;
    bool result = proto.deserializeJson(buffer, msg);
    
    BOOST_CHECK(!result);
    BOOST_CHECK(msg.errorReason.find("not an object") != std::string::npos);
}

// ======================== Stats проверка ========================

BOOST_AUTO_TEST_CASE(Stats_OkIncrementsOnSuccess) {
    uint64_t before = proto.stats().ok;
    uint64_t beforeFails = proto.stats().fails;
    
    boost::json::object data = {{"test", true}};
    MessageHeader dummy;
    std::vector<uint8_t> buffer = createValidBuffer(data, dummy);
    IncomingMessage msg;
    proto.deserializeJson(buffer, msg);
    
    BOOST_CHECK_EQUAL(proto.stats().ok, before + 1);
    BOOST_CHECK_EQUAL(proto.stats().fails, beforeFails);
}

BOOST_AUTO_TEST_CASE(Stats_FailsIncrementsOnError) {
    uint64_t before = proto.stats().fails;
    uint64_t beforeOk = proto.stats().ok;
    
    std::vector<uint8_t> emptyBuf;
    IncomingMessage msg;
    proto.deserializeJson(emptyBuf, msg);
    
    BOOST_CHECK_EQUAL(proto.stats().fails, before + 1);
    BOOST_CHECK_EQUAL(proto.stats().ok, beforeOk);
}

BOOST_AUTO_TEST_CASE(Stats_SequentialMix) {
    // Успешный вызов
    boost::json::object validData = {{"success", 1}};
    MessageHeader dummy;
    std::vector<uint8_t> goodBuf = createValidBuffer(validData, dummy);
    IncomingMessage msg1;
    proto.deserializeJson(goodBuf, msg1);
    
    // Ошибочный вызов (неверная магия)
    goodBuf[0] ^= 0xFF;
    IncomingMessage msg2;
    proto.deserializeJson(goodBuf, msg2);
    
    BOOST_CHECK_EQUAL(proto.stats().ok, 1);
    BOOST_CHECK_EQUAL(proto.stats().fails, 1);
}


BOOST_AUTO_TEST_SUITE_END()