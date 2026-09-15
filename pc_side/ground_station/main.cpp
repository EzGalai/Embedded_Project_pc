/**
 * @file main.cpp
 * @brief ground_station — Phase 9: connects to central_computer's GS-facing
 * link and performs the "trivial handshake" (PROJECT_PLAN.md §6 Phase 9).
 * Phase 14 adds the real queries: Gs_RequestLog/Gs_RequestEvents send
 * GS_GET_LOG_REQ/GS_GET_EVENTS_REQ and print the returned records.
 *
 * The handshake itself is deliberately outside the TLV protocol — no
 * protocol.h message exists for it, since it only proves the CC<->GS
 * Ethernet link works, independent of the LNC link/bridge process.
 */

#include "protocol.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define CC_GS_PORT 9000
#define GS_HANDSHAKE_GREETING "GS_HELLO"
#define GS_HANDSHAKE_REPLY    "CC_HELLO_ACK"

/**
 * @brief Formats a Unix timestamp as "YYYY-MM-DD HH:MM:SS" for display —
 * same convention central_computer uses (see its FormatUnixTime).
 */
static void FormatUnixTime(uint32_t timestamp, char *outBuf, size_t bufSize)
{
    time_t t = (time_t)timestamp;
    struct tm tmVal;
    localtime_r(&t, &tmVal);
    strftime(outBuf, bufSize, "%Y-%m-%d %H:%M:%S", &tmVal);
}

/**
 * @brief Sends a length-prefixed TLV message to central_computer's GS link.
 */
static void GsSend(int fd, uint8_t tag, const uint8_t *value, uint16_t valueLen)
{
    uint8_t message[128];
    uint16_t messageLen;
    Protocol_EncodeTLV(tag, value, valueLen, message, sizeof(message), &messageLen);

    uint8_t prefix[4];
    Protocol_PutU32(prefix, messageLen);
    send(fd, prefix, sizeof(prefix), 0);
    send(fd, message, messageLen, 0);
}

/**
 * @brief Reads one length-prefixed TLV message into a dynamically-sized
 * buffer (GS_GET_LOG_RESP/GS_GET_EVENTS_RESP can be larger than any fixed
 * buffer, since central_computer doesn't paginate this link).
 * @return true on success, false on disconnect or malformed data.
 */
static bool GsRecvMessage(int fd, uint8_t *outTag, const uint8_t **outValue, uint16_t *outValueLen,
                           std::vector<uint8_t> &storage)
{
    uint8_t prefix[4];
    if (recv(fd, prefix, sizeof(prefix), MSG_WAITALL) != sizeof(prefix)) return false;

    uint32_t msgLen;
    Protocol_GetU32(prefix, &msgLen);
    if (msgLen == 0) return false;

    storage.resize(msgLen);
    if (recv(fd, storage.data(), msgLen, MSG_WAITALL) != (ssize_t)msgLen) return false;

    uint16_t consumed;
    return Protocol_DecodeTLV(storage.data(), (uint16_t)msgLen, outTag, outValue, outValueLen, &consumed) == PROTO_OK;
}

/**
 * @brief Prints one MEASUREMENT_RECORD's fields.
 */
static void PrintMeasurementRecord(const uint8_t *record, uint16_t recordLen)
{
    const uint8_t *field;
    uint16_t fieldLen;

    uint32_t timestamp = 0;
    int16_t temperature = 0;
    uint8_t humidity = 0, mode = 0;
    uint16_t light = 0, battery = 0;

    if (Protocol_FindField(record, recordLen, PROTO_FIELD_TIMESTAMP, &field, &fieldLen) == PROTO_OK) Protocol_GetU32(field, &timestamp);
    if (Protocol_FindField(record, recordLen, PROTO_FIELD_TEMPERATURE, &field, &fieldLen) == PROTO_OK) { uint16_t raw; Protocol_GetU16(field, &raw); temperature = (int16_t)raw; }
    if (Protocol_FindField(record, recordLen, PROTO_FIELD_HUMIDITY, &field, &fieldLen) == PROTO_OK) humidity = field[0];
    if (Protocol_FindField(record, recordLen, PROTO_FIELD_LIGHT, &field, &fieldLen) == PROTO_OK) Protocol_GetU16(field, &light);
    if (Protocol_FindField(record, recordLen, PROTO_FIELD_BATTERY_VOLTAGE, &field, &fieldLen) == PROTO_OK) Protocol_GetU16(field, &battery);
    if (Protocol_FindField(record, recordLen, PROTO_FIELD_MODE, &field, &fieldLen) == PROTO_OK) mode = field[0];

    char timeStr[32];
    FormatUnixTime(timestamp, timeStr, sizeof(timeStr));
    printf("  [%s] temp=%.1fC humidity=%u%% light=%u%% battery=%u%% mode=%u\n",
           timeStr, temperature / 10.0, humidity,
           (unsigned)(light * 100 / 4095), (unsigned)(battery * 100 / 3300), mode);
}

/**
 * @brief Sends GS_GET_LOG_REQ(submarineId, start, end) and prints every
 * returned MEASUREMENT_RECORD. No pagination on this link — the whole
 * result arrives in one response.
 */
static void Gs_RequestLog(int fd, const std::string &submarineId, uint32_t start, uint32_t end)
{
    printf("GS_GET_LOG(%s, %u, %u):\n", submarineId.c_str(), (unsigned)start, (unsigned)end);

    uint8_t payload[80];
    uint16_t payloadLen = 0, written;

    Protocol_EncodeTLV(PROTO_FIELD_SUBMARINE_ID, (const uint8_t *)submarineId.data(), (uint16_t)submarineId.size(),
                        payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
    payloadLen = (uint16_t)(payloadLen + written);

    uint8_t valueBuf[4];
    Protocol_PutU32(valueBuf, start);
    Protocol_EncodeTLV(PROTO_FIELD_TIME_RANGE_START, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
    payloadLen = (uint16_t)(payloadLen + written);

    Protocol_PutU32(valueBuf, end);
    Protocol_EncodeTLV(PROTO_FIELD_TIME_RANGE_END, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
    payloadLen = (uint16_t)(payloadLen + written);

    GsSend(fd, PROTO_TAG_GS_GET_LOG_REQ, payload, payloadLen);

    uint8_t tag;
    const uint8_t *value;
    uint16_t valueLen;
    std::vector<uint8_t> storage;
    if (!GsRecvMessage(fd, &tag, &value, &valueLen, storage)) {
        fprintf(stderr, "ground_station: GS_GET_LOG_RESP not received\n");
        return;
    }
    if (tag != PROTO_TAG_GS_GET_LOG_RESP) {
        fprintf(stderr, "ground_station: expected GS_GET_LOG_RESP, got tag 0x%02X\n", tag);
        return;
    }

    const uint8_t *field;
    uint16_t fieldLen;
    uint8_t status = 0;
    if (Protocol_FindField(value, valueLen, PROTO_FIELD_STATUS, &field, &fieldLen) == PROTO_OK) status = field[0];

    if (status == PROTO_STATUS_NO_DATA_FOUND) {
        printf("  no data found in range\n");
        return;
    }
    if (status != PROTO_STATUS_SUCCESS) {
        printf("  status=%u\n", status);
        return;
    }

    /* Protocol_FindField only returns the first match of a tag — walk the
       value buffer manually to visit every MEASUREMENT_RECORD. */
    int totalRecords = 0;
    uint16_t offset = 0;
    while ((uint16_t)(offset + 3) <= valueLen) {
        uint8_t fTag = value[offset];
        uint16_t fLen;
        Protocol_GetU16(value + offset + 1, &fLen);

        if (fTag == PROTO_FIELD_MEASUREMENT_RECORD) {
            PrintMeasurementRecord(value + offset + 3, fLen);
            totalRecords++;
        }
        offset = (uint16_t)(offset + 3 + fLen);
    }
    printf("GS_GET_LOG: done, %d record(s) total\n", totalRecords);
}

/**
 * @brief Sends GS_GET_EVENTS_REQ(submarineId, start, end) and prints every
 * returned EVENT_RECORD (including its nested MEASUREMENT_RECORD, for
 * Monitor-sourced events). Same no-pagination shape as Gs_RequestLog.
 */
static void Gs_RequestEvents(int fd, const std::string &submarineId, uint32_t start, uint32_t end)
{
    printf("GS_GET_EVENTS(%s, %u, %u):\n", submarineId.c_str(), (unsigned)start, (unsigned)end);

    uint8_t payload[80];
    uint16_t payloadLen = 0, written;

    Protocol_EncodeTLV(PROTO_FIELD_SUBMARINE_ID, (const uint8_t *)submarineId.data(), (uint16_t)submarineId.size(),
                        payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
    payloadLen = (uint16_t)(payloadLen + written);

    uint8_t valueBuf[4];
    Protocol_PutU32(valueBuf, start);
    Protocol_EncodeTLV(PROTO_FIELD_TIME_RANGE_START, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
    payloadLen = (uint16_t)(payloadLen + written);

    Protocol_PutU32(valueBuf, end);
    Protocol_EncodeTLV(PROTO_FIELD_TIME_RANGE_END, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
    payloadLen = (uint16_t)(payloadLen + written);

    GsSend(fd, PROTO_TAG_GS_GET_EVENTS_REQ, payload, payloadLen);

    uint8_t tag;
    const uint8_t *value;
    uint16_t valueLen;
    std::vector<uint8_t> storage;
    if (!GsRecvMessage(fd, &tag, &value, &valueLen, storage)) {
        fprintf(stderr, "ground_station: GS_GET_EVENTS_RESP not received\n");
        return;
    }
    if (tag != PROTO_TAG_GS_GET_EVENTS_RESP) {
        fprintf(stderr, "ground_station: expected GS_GET_EVENTS_RESP, got tag 0x%02X\n", tag);
        return;
    }

    const uint8_t *field;
    uint16_t fieldLen;
    uint8_t status = 0;
    if (Protocol_FindField(value, valueLen, PROTO_FIELD_STATUS, &field, &fieldLen) == PROTO_OK) status = field[0];

    if (status == PROTO_STATUS_NO_DATA_FOUND) {
        printf("  no data found in range\n");
        return;
    }
    if (status != PROTO_STATUS_SUCCESS) {
        printf("  status=%u\n", status);
        return;
    }

    int totalRecords = 0;
    uint16_t offset = 0;
    while ((uint16_t)(offset + 3) <= valueLen) {
        uint8_t fTag = value[offset];
        uint16_t fLen;
        Protocol_GetU16(value + offset + 1, &fLen);
        const uint8_t *fValue = value + offset + 3;

        if (fTag == PROTO_FIELD_EVENT_RECORD) {
            uint32_t ts = 0;
            uint8_t eventType = 0, eventSource = 0;
            const uint8_t *ef;
            uint16_t efLen;

            if (Protocol_FindField(fValue, fLen, PROTO_FIELD_TIMESTAMP, &ef, &efLen) == PROTO_OK) Protocol_GetU32(ef, &ts);
            if (Protocol_FindField(fValue, fLen, PROTO_FIELD_EVENT_TYPE, &ef, &efLen) == PROTO_OK) eventType = ef[0];
            if (Protocol_FindField(fValue, fLen, PROTO_FIELD_EVENT_SOURCE, &ef, &efLen) == PROTO_OK) eventSource = ef[0];

            char timeStr[32];
            FormatUnixTime(ts, timeStr, sizeof(timeStr));
            printf("  [%s] type=%u source=%u", timeStr, eventType, eventSource);

            const uint8_t *measurement;
            uint16_t measurementLen;
            if (Protocol_FindField(fValue, fLen, PROTO_FIELD_MEASUREMENT_RECORD, &measurement, &measurementLen) == PROTO_OK) {
                int16_t temperature = 0;
                uint8_t humidity = 0, mode = 0;
                uint16_t light = 0, battery = 0;

                if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_TEMPERATURE, &ef, &efLen) == PROTO_OK) { uint16_t raw; Protocol_GetU16(ef, &raw); temperature = (int16_t)raw; }
                if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_HUMIDITY, &ef, &efLen) == PROTO_OK) humidity = ef[0];
                if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_LIGHT, &ef, &efLen) == PROTO_OK) Protocol_GetU16(ef, &light);
                if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_BATTERY_VOLTAGE, &ef, &efLen) == PROTO_OK) Protocol_GetU16(ef, &battery);
                if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_MODE, &ef, &efLen) == PROTO_OK) mode = ef[0];

                printf(" | temp=%.1fC humidity=%u%% light=%u%% battery=%u%% mode=%u",
                       temperature / 10.0, humidity, (unsigned)(light * 100 / 4095),
                       (unsigned)(battery * 100 / 3300), mode);
            }
            printf("\n");
            totalRecords++;
        }
        offset = (uint16_t)(offset + 3 + fLen);
    }
    printf("GS_GET_EVENTS: done, %d record(s) total\n", totalRecords);
}

int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "ground_station: socket() failed: %s\n", strerror(errno));
        return 1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(CC_GS_PORT);

    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "ground_station: connect() to central_computer failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("ground_station: connected to central_computer on 127.0.0.1:%d\n", CC_GS_PORT);

    /* Send the length-prefixed greeting */
    uint16_t greetingLen = (uint16_t)strlen(GS_HANDSHAKE_GREETING);
    uint8_t prefix[4] = {0, 0, (uint8_t)(greetingLen >> 8), (uint8_t)greetingLen};
    if (send(fd, prefix, sizeof(prefix), 0) < 0 || send(fd, GS_HANDSHAKE_GREETING, greetingLen, 0) < 0) {
        fprintf(stderr, "ground_station: send() failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("ground_station: sent greeting: %s\n", GS_HANDSHAKE_GREETING);

    /* Block waiting for the length-prefixed reply */
    if (recv(fd, prefix, sizeof(prefix), MSG_WAITALL) != sizeof(prefix)) {
        fprintf(stderr, "ground_station: failed to read reply length prefix: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    uint32_t replyLen = ((uint32_t)prefix[0] << 24) | ((uint32_t)prefix[1] << 16) |
                         ((uint32_t)prefix[2] << 8)  | (uint32_t)prefix[3];

    char reply[64] = {0};
    if (replyLen >= sizeof(reply) || recv(fd, reply, replyLen, MSG_WAITALL) != (ssize_t)replyLen) {
        fprintf(stderr, "ground_station: failed to read reply\n");
        close(fd);
        return 1;
    }
    printf("ground_station: received reply: %s\n", reply);

    bool match = (strcmp(reply, GS_HANDSHAKE_REPLY) == 0);
    printf("Phase 9 test: %s\n", match ? "PASSED" : "FAILED");

    if (match) {
        /* --- Phase 14: real GS queries ---
           A wide-ish range covering "today" on this machine's own clock
           (matches how DCA dates its files — see dca.cpp's TodayDateString)
           plus a day of margin either side, so this works regardless of
           exactly when central_computer happened to receive the data. */
        time_t now = time(nullptr);
        uint32_t nowTs = (uint32_t)now;
        uint32_t dayStart = (nowTs > 86400) ? (nowTs - 86400) : 0;
        uint32_t dayEnd = nowTs + 86400;

        Gs_RequestLog(fd, "LNC-01", dayStart, dayEnd);
        Gs_RequestEvents(fd, "LNC-01", dayStart, dayEnd);
    }

    close(fd);
    return match ? 0 : 1;
}
