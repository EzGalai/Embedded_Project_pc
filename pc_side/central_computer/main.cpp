/**
 * @file main.cpp
 * @brief central_computer core — Phase 7 (KEEP_ALIVE) + Phase 8 (Get/Set
 * time round-trip). See PROJECT_PLAN.md §5.1, §5.3, §6 Phase 8.
 */

#include "protocol.h"
#include "dca.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BRIDGE_TCP_PORT 5100
#define GS_TCP_PORT 9000
#define GS_HANDSHAKE_GREETING "GS_HELLO"
#define GS_HANDSHAKE_REPLY    "CC_HELLO_ACK"

/* Fixed for this project's single-LNC setup — a real per-connection ID
   isn't threaded through the live wire protocol until Phase 16's Fleet
   Management work, per PROJECT_PLAN.md §4.17's discussion. */
#define SUBMARINE_ID "LNC-01"

/**
 * @brief Formats a Unix timestamp (seconds since 1970-01-01 UTC) as a
 * human-readable "YYYY-MM-DD HH:MM:SS" string, for display only — the wire
 * protocol keeps the raw value, matching how light/battery are converted to
 * percentages for display but kept raw on the wire.
 * @param timestamp Seconds since the Unix epoch.
 * @param outBuf Destination buffer.
 * @param bufSize Capacity of outBuf.
 */
static void FormatUnixTime(uint32_t timestamp, char *outBuf, size_t bufSize)
{
    time_t t = (time_t)timestamp;
    struct tm tmVal;
    localtime_r(&t, &tmVal);
    strftime(outBuf, bufSize, "%Y-%m-%d %H:%M:%S", &tmVal);
}

static int CcCore_LncConnect(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "central_computer: socket() failed: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(BRIDGE_TCP_PORT);

    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "central_computer: connect() to lnc_bridge failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static uint16_t CcCore_LncRecv(int fd, uint8_t *outBuf, uint16_t maxLen)
{
    uint8_t prefix[4];
    if (recv(fd, prefix, sizeof(prefix), MSG_WAITALL) != sizeof(prefix)) {
        return 0;
    }

    uint32_t len;
    Protocol_GetU32(prefix, &len);

    if (len > maxLen) {
        fprintf(stderr, "central_computer: message too large (%u bytes)\n", (unsigned)len);
        return 0;
    }

    if (recv(fd, outBuf, len, MSG_WAITALL) != (ssize_t)len) {
        return 0;
    }

    return (uint16_t)len;
}

/**
 * @brief TLV-encode a message and send it length-prefixed to lnc_bridge.
 * @param fd Connected socket.
 * @param tag Message tag.
 * @param value Value bytes (may be nullptr if valueLen is 0).
 * @param valueLen Length of value.
 */
static void CcCore_LncSend(int fd, uint8_t tag, const uint8_t *value, uint16_t valueLen)
{
    uint8_t message[32];
    uint16_t messageLen;
    Protocol_EncodeTLV(tag, value, valueLen, message, sizeof(message), &messageLen);

    uint8_t prefix[4];
    Protocol_PutU32(prefix, messageLen);
    send(fd, prefix, sizeof(prefix), 0);
    send(fd, message, messageLen, 0);
}

static void CcCore_PrintKeepAlive(const uint8_t *value, uint16_t len)
{
    const uint8_t *field;
    uint16_t fieldLen;

    uint32_t timestamp = 0;
    if (Protocol_FindField(value, len, PROTO_FIELD_TIMESTAMP, &field, &fieldLen) == PROTO_OK) {
        Protocol_GetU32(field, &timestamp);
    }

    uint8_t mode = 0;
    if (Protocol_FindField(value, len, PROTO_FIELD_MODE, &field, &fieldLen) == PROTO_OK) {
        mode = field[0];
    }

    char timeStr[32];
    FormatUnixTime(timestamp, timeStr, sizeof(timeStr));

    const uint8_t *measurement;
    uint16_t measurementLen;
    if (Protocol_FindField(value, len, PROTO_FIELD_MEASUREMENT_RECORD, &measurement, &measurementLen) != PROTO_OK) {
        printf("KEEP_ALIVE: time=%s mode=%u (no MEASUREMENT_RECORD found)\n", timeStr, mode);
        return;
    }

    int16_t temperature = 0;
    uint8_t humidity = 0;
    uint16_t light = 0, battery = 0;

    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_TEMPERATURE, &field, &fieldLen) == PROTO_OK) {
        uint16_t raw;
        Protocol_GetU16(field, &raw);
        temperature = (int16_t)raw;
    }
    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_HUMIDITY, &field, &fieldLen) == PROTO_OK) {
        humidity = field[0];
    }
    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_LIGHT, &field, &fieldLen) == PROTO_OK) {
        Protocol_GetU16(field, &light);
    }
    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_BATTERY_VOLTAGE, &field, &fieldLen) == PROTO_OK) {
        Protocol_GetU16(field, &battery);
    }

    DCA_StoreMeasurement(SUBMARINE_ID, timestamp, temperature, humidity, light, battery, mode);

    printf("KEEP_ALIVE: time=%s mode=%u | temp=%.1fC humidity=%u%% light=%u%% battery=%u%%\n",
       timeStr, mode, temperature / 10.0, humidity,
       (unsigned)(light * 100 / 4095), (unsigned)(battery * 100 / 3300));

}

/* Forward declaration — defined further below, needed here since
   CcCore_LncRecvMessage transparently prints an interleaved EVENT_REPORT
   the same way it already does for KEEP_ALIVE. */
static void CcCore_PrintEventReport(const uint8_t *value, uint16_t len);

/**
 * @brief Reads and decodes one message from lnc_bridge, transparently printing
 * (and skipping) any KEEP_ALIVE that arrives along the way.
 * @param fd Connected socket.
 * @param outTag Set to the decoded tag of the first non-KEEP_ALIVE message.
 * @param outValue Set to point at that message's Value.
 * @param outValueLen Set to that Value's length.
 * @param storage Caller-provided buffer outValue points into (must outlive outValue's use).
 * @param storageCap Capacity of storage.
 * @return true on success, false on disconnect or malformed data.
 */
static bool CcCore_LncRecvMessage(int fd, uint8_t *outTag, const uint8_t **outValue, uint16_t *outValueLen,
                                   uint8_t *storage, uint16_t storageCap)
{
    for (;;) {
        uint16_t payloadLen = CcCore_LncRecv(fd, storage, storageCap);
        if (payloadLen == 0) {
            return false;
        }

        uint16_t consumed;
        if (Protocol_DecodeTLV(storage, payloadLen, outTag, outValue, outValueLen, &consumed) != PROTO_OK) {
            fprintf(stderr, "central_computer: malformed message from lnc_bridge\n");
            continue;
        }

        if (*outTag == PROTO_TAG_KEEP_ALIVE) {
            CcCore_PrintKeepAlive(*outValue, *outValueLen);
            continue; /* not what we're waiting for — keep listening */
        }
        if (*outTag == PROTO_TAG_EVENT_REPORT) {
            CcCore_PrintEventReport(*outValue, *outValueLen);
            continue; /* not what we're waiting for — keep listening */
        }

        return true;
    }
}

/**
 * @brief Sends GET_TIME_REQ and waits for GET_TIME_RESP.
 * @param fd Connected socket.
 * @param outTime Set to the decoded TIMESTAMP on success.
 * @return true on success.
 */
static bool CcCore_GetTime(int fd, uint32_t *outTime)
{
    CcCore_LncSend(fd, PROTO_TAG_GET_TIME_REQ, nullptr, 0);

    uint8_t storage[256];
    uint8_t tag;
    const uint8_t *value;
    uint16_t valueLen;

    if (!CcCore_LncRecvMessage(fd, &tag, &value, &valueLen, storage, sizeof(storage))) {
        return false;
    }
    if (tag != PROTO_TAG_GET_TIME_RESP || valueLen < 4) {
        fprintf(stderr, "central_computer: expected GET_TIME_RESP, got tag 0x%02X\n", tag);
        return false;
    }

    Protocol_GetU32(value, outTime);
    return true;
}

/**
 * @brief Sends SET_RTC_REQ with newTime and waits for CONFIG_ACK.
 * @param fd Connected socket.
 * @param newTime Timestamp to set.
 * @param outStatus Set to the ACK's STATUS on success.
 * @return true on success.
 */
static bool CcCore_SetRtc(int fd, uint32_t newTime, ProtoStatus_t *outStatus)
{
    uint8_t valueBuf[4];
    Protocol_PutU32(valueBuf, newTime);
    CcCore_LncSend(fd, PROTO_TAG_SET_RTC_REQ, valueBuf, 4);

    uint8_t storage[256];
    uint8_t tag;
    const uint8_t *value;
    uint16_t valueLen;

    if (!CcCore_LncRecvMessage(fd, &tag, &value, &valueLen, storage, sizeof(storage))) {
        return false;
    }
    if (tag != PROTO_TAG_CONFIG_ACK || valueLen < 1) {
        fprintf(stderr, "central_computer: expected CONFIG_ACK, got tag 0x%02X\n", tag);
        return false;
    }

    *outStatus = (ProtoStatus_t)value[0];
    return true;
}

/**
 * @brief Sends SET_BATTERY_WARNING_MIN with newMinMv and waits for CONFIG_ACK.
 * Phase 12 test: proves a SET_* config command reaches Config_ApplyUpdate
 * and gets acknowledged.
 * @param fd Connected socket.
 * @param newMinMv New battery-warning-minimum threshold, in mV.
 * @param outStatus Set to the ACK's STATUS on success.
 * @return true on success.
 */
static bool CcCore_SetBatteryWarningMin(int fd, uint16_t newMinMv, ProtoStatus_t *outStatus)
{
    uint8_t fieldBuf[2];
    Protocol_PutU16(fieldBuf, newMinMv);

    uint8_t value[8];
    uint16_t valueLen;
    Protocol_EncodeTLV(PROTO_FIELD_BATTERY_MIN, fieldBuf, 2, value, sizeof(value), &valueLen);

    CcCore_LncSend(fd, PROTO_TAG_SET_BATTERY_WARNING_MIN, value, valueLen);

    uint8_t storage[256];
    uint8_t tag;
    const uint8_t *respValue;
    uint16_t respValueLen;
    if (!CcCore_LncRecvMessage(fd, &tag, &respValue, &respValueLen, storage, sizeof(storage))) {
        return false;
    }
    if (tag != PROTO_TAG_CONFIG_ACK || respValueLen < 1) {
        fprintf(stderr, "central_computer: expected CONFIG_ACK, got tag 0x%02X\n", tag);
        return false;
    }

    *outStatus = (ProtoStatus_t)respValue[0];
    return true;
}

/**
 * @brief Sends GET_MEASUREMENTS_REQ for [startTime, endTime] and prints
 * every returned MEASUREMENT_RECORD, automatically following the
 * CURSOR_DAY/CURSOR_OFFSET pagination protocol (Phase 13) until a response
 * arrives with no cursor attached, meaning the whole range has been sent.
 * @param fd Connected socket.
 * @param startTime Inclusive range start (Unix timestamp).
 * @param endTime Inclusive range end (Unix timestamp).
 */
static void CcCore_GetMeasurements(int fd, uint32_t startTime, uint32_t endTime)
{
    printf("GET_MEASUREMENTS(%u, %u):\n", (unsigned)startTime, (unsigned)endTime);

    bool haveCursor = false;
    uint32_t cursorDay = 0, cursorOffset = 0;
    int totalRecords = 0;

    for (;;) {
        uint8_t valueBuf[4];
        uint8_t payload[32];
        uint16_t payloadLen = 0, written;

        Protocol_PutU32(valueBuf, startTime);
        Protocol_EncodeTLV(PROTO_FIELD_TIME_RANGE_START, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
        payloadLen = (uint16_t)(payloadLen + written);

        Protocol_PutU32(valueBuf, endTime);
        Protocol_EncodeTLV(PROTO_FIELD_TIME_RANGE_END, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
        payloadLen = (uint16_t)(payloadLen + written);

        if (haveCursor) {
            Protocol_PutU32(valueBuf, cursorDay);
            Protocol_EncodeTLV(PROTO_FIELD_CURSOR_DAY, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
            payloadLen = (uint16_t)(payloadLen + written);

            Protocol_PutU32(valueBuf, cursorOffset);
            Protocol_EncodeTLV(PROTO_FIELD_CURSOR_OFFSET, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
            payloadLen = (uint16_t)(payloadLen + written);
        }

        CcCore_LncSend(fd, PROTO_TAG_GET_MEASUREMENTS_REQ, payload, payloadLen);

        uint8_t storage[300];
        uint8_t tag;
        const uint8_t *value;
        uint16_t valueLen;

        if (!CcCore_LncRecvMessage(fd, &tag, &value, &valueLen, storage, sizeof(storage))) {
            fprintf(stderr, "central_computer: GET_MEASUREMENTS_RESP not received\n");
            return;
        }
        if (tag != PROTO_TAG_GET_MEASUREMENTS_RESP) {
            fprintf(stderr, "central_computer: expected GET_MEASUREMENTS_RESP, got tag 0x%02X\n", tag);
            return;
        }

        const uint8_t *field;
        uint16_t fieldLen;
        uint8_t status = 0;
        if (Protocol_FindField(value, valueLen, PROTO_FIELD_STATUS, &field, &fieldLen) == PROTO_OK) {
            status = field[0];
        }

        if (status == PROTO_STATUS_NO_DATA_FOUND) {
            printf("  no data found in range\n");
            return;
        }
        if (status != PROTO_STATUS_SUCCESS) {
            printf("  status=%u\n", status);
            return;
        }

        /* Protocol_FindField only returns the first match of a tag — since
           a response can carry multiple MEASUREMENT_RECORD fields, walk
           the value buffer manually (tag+len+value, advancing by 3+len). */
        bool responseHasCursor = false;
        uint16_t offset = 0;
        while ((uint16_t)(offset + 3) <= valueLen) {
            uint8_t fTag = value[offset];
            uint16_t fLen;
            Protocol_GetU16(value + offset + 1, &fLen);
            const uint8_t *fValue = value + offset + 3;

            if (fTag == PROTO_FIELD_MEASUREMENT_RECORD) {
                uint32_t ts = 0;
                int16_t temperature = 0;
                uint8_t humidity = 0, mode = 0;
                uint16_t light = 0, battery = 0;
                const uint8_t *mf;
                uint16_t mfLen;

                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_TIMESTAMP, &mf, &mfLen) == PROTO_OK) Protocol_GetU32(mf, &ts);
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_TEMPERATURE, &mf, &mfLen) == PROTO_OK) { uint16_t raw; Protocol_GetU16(mf, &raw); temperature = (int16_t)raw; }
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_HUMIDITY, &mf, &mfLen) == PROTO_OK) humidity = mf[0];
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_LIGHT, &mf, &mfLen) == PROTO_OK) Protocol_GetU16(mf, &light);
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_BATTERY_VOLTAGE, &mf, &mfLen) == PROTO_OK) Protocol_GetU16(mf, &battery);
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_MODE, &mf, &mfLen) == PROTO_OK) mode = mf[0];

                char timeStr[32];
                FormatUnixTime(ts, timeStr, sizeof(timeStr));
                printf("  [%s] temp=%.1fC humidity=%u%% light=%u%% battery=%u%% mode=%u\n",
                       timeStr, temperature / 10.0, humidity,
                       (unsigned)(light * 100 / 4095), (unsigned)(battery * 100 / 3300), mode);
                totalRecords++;
            } else if (fTag == PROTO_FIELD_CURSOR_DAY) {
                Protocol_GetU32(fValue, &cursorDay);
                responseHasCursor = true;
            } else if (fTag == PROTO_FIELD_CURSOR_OFFSET) {
                Protocol_GetU32(fValue, &cursorOffset);
            }

            offset = (uint16_t)(offset + 3 + fLen);
        }

        if (!responseHasCursor) {
            printf("GET_MEASUREMENTS: done, %d record(s) total\n", totalRecords);
            return;
        }
        haveCursor = true;
    }
}

/**
 * @brief Sends GET_EVENTS_REQ for [startTime, endTime] and prints every
 * returned EVENT_RECORD (including the nested MEASUREMENT_RECORD carried
 * by Monitor-sourced events), automatically following the
 * CURSOR_DAY/CURSOR_OFFSET pagination protocol until a response arrives
 * with no cursor attached — same mechanism as CcCore_GetMeasurements.
 * @param fd Connected socket.
 * @param startTime Inclusive range start (Unix timestamp).
 * @param endTime Inclusive range end (Unix timestamp).
 */
static void CcCore_GetEvents(int fd, uint32_t startTime, uint32_t endTime)
{
    printf("GET_EVENTS(%u, %u):\n", (unsigned)startTime, (unsigned)endTime);

    bool haveCursor = false;
    uint32_t cursorDay = 0, cursorOffset = 0;
    int totalRecords = 0;

    for (;;) {
        uint8_t valueBuf[4];
        uint8_t payload[32];
        uint16_t payloadLen = 0, written;

        Protocol_PutU32(valueBuf, startTime);
        Protocol_EncodeTLV(PROTO_FIELD_TIME_RANGE_START, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
        payloadLen = (uint16_t)(payloadLen + written);

        Protocol_PutU32(valueBuf, endTime);
        Protocol_EncodeTLV(PROTO_FIELD_TIME_RANGE_END, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
        payloadLen = (uint16_t)(payloadLen + written);

        if (haveCursor) {
            Protocol_PutU32(valueBuf, cursorDay);
            Protocol_EncodeTLV(PROTO_FIELD_CURSOR_DAY, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
            payloadLen = (uint16_t)(payloadLen + written);

            Protocol_PutU32(valueBuf, cursorOffset);
            Protocol_EncodeTLV(PROTO_FIELD_CURSOR_OFFSET, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
            payloadLen = (uint16_t)(payloadLen + written);
        }

        CcCore_LncSend(fd, PROTO_TAG_GET_EVENTS_REQ, payload, payloadLen);

        uint8_t storage[300];
        uint8_t tag;
        const uint8_t *value;
        uint16_t valueLen;

        if (!CcCore_LncRecvMessage(fd, &tag, &value, &valueLen, storage, sizeof(storage))) {
            fprintf(stderr, "central_computer: GET_EVENTS_RESP not received\n");
            return;
        }
        if (tag != PROTO_TAG_GET_EVENTS_RESP) {
            fprintf(stderr, "central_computer: expected GET_EVENTS_RESP, got tag 0x%02X\n", tag);
            return;
        }

        const uint8_t *field;
        uint16_t fieldLen;
        uint8_t status = 0;
        if (Protocol_FindField(value, valueLen, PROTO_FIELD_STATUS, &field, &fieldLen) == PROTO_OK) {
            status = field[0];
        }

        if (status == PROTO_STATUS_NO_DATA_FOUND) {
            printf("  no data found in range\n");
            return;
        }
        if (status != PROTO_STATUS_SUCCESS) {
            printf("  status=%u\n", status);
            return;
        }

        /* Protocol_FindField only returns the first match of a tag — since
           a response can carry multiple EVENT_RECORD fields, walk the
           value buffer manually (tag+len+value, advancing by 3+len). */
        bool responseHasCursor = false;
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
            } else if (fTag == PROTO_FIELD_CURSOR_DAY) {
                Protocol_GetU32(fValue, &cursorDay);
                responseHasCursor = true;
            } else if (fTag == PROTO_FIELD_CURSOR_OFFSET) {
                Protocol_GetU32(fValue, &cursorOffset);
            }

            offset = (uint16_t)(offset + 3 + fLen);
        }

        if (!responseHasCursor) {
            printf("GET_EVENTS: done, %d record(s) total\n", totalRecords);
            return;
        }
        haveCursor = true;
    }
}

/* Cap on how many record bytes accumulate into one GS_GET_LOG_RESP/
   GS_GET_EVENTS_RESP payload — the TLV length field is 16-bit (max 65535),
   so this stays safely under that regardless of how many records
   DCA_QueryMeasurements/DCA_QueryEvents returned (no pagination on this
   link, unlike the LNC's tiny fixed buffers — a plain TCP link doesn't
   need it at this project's scale, but the 16-bit length field itself
   still needs respecting). */
static const size_t GS_RESP_MAX_PAYLOAD = 60000;

/**
 * @brief Sends an already-built message (tag + payload, e.g. from
 * WrapMessage) length-prefixed to a GS socket, without needing the whole
 * thing to fit through Protocol_EncodeTLV's fixed-size-output-buffer
 * signature — used for GS_GET_LOG_RESP/GS_GET_EVENTS_RESP, whose payload
 * can be much larger than any of this project's other fixed buffers.
 */
static void GsSendRaw(int fd, const std::vector<uint8_t> &message)
{
    uint8_t prefix[4];
    Protocol_PutU32(prefix, (uint32_t)message.size());
    send(fd, prefix, sizeof(prefix), 0);
    send(fd, message.data(), message.size(), 0);
}

/**
 * @brief Reads one length-prefixed TLV message from a GS socket.
 * @return true on success, false on disconnect or malformed data.
 */
static bool GsRecvMessage(int fd, uint8_t *outTag, const uint8_t **outValue, uint16_t *outValueLen,
                           uint8_t *storage, uint16_t storageCap)
{
    uint8_t prefix[4];
    if (recv(fd, prefix, sizeof(prefix), MSG_WAITALL) != sizeof(prefix)) return false;

    uint32_t msgLen;
    Protocol_GetU32(prefix, &msgLen);
    if (msgLen == 0 || msgLen > storageCap) return false;
    if (recv(fd, storage, msgLen, MSG_WAITALL) != (ssize_t)msgLen) return false;

    uint16_t consumed;
    return Protocol_DecodeTLV(storage, (uint16_t)msgLen, outTag, outValue, outValueLen, &consumed) == PROTO_OK;
}

/**
 * @brief Wraps a payload (already-concatenated fields) in an outer TLV tag
 * header, built manually rather than via Protocol_EncodeTLV since the
 * payload can exceed any reasonable fixed-size output buffer — same
 * tag(1)+len(2, big-endian)+value layout Protocol_EncodeTLV itself uses.
 */
static std::vector<uint8_t> WrapMessage(uint8_t tag, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> message;
    message.reserve(payload.size() + 3);
    message.push_back(tag);

    uint8_t lenBytes[2];
    Protocol_PutU16(lenBytes, (uint16_t)payload.size());
    message.push_back(lenBytes[0]);
    message.push_back(lenBytes[1]);

    message.insert(message.end(), payload.begin(), payload.end());
    return message;
}

/**
 * @brief Encodes one DcaMeasurement as a MEASUREMENT_RECORD TLV and
 * appends its bytes to out.
 */
static void AppendMeasurementRecord(std::vector<uint8_t> &out, const DcaMeasurement &m)
{
    uint8_t measurement[40];
    uint16_t measurementLen = 0, written;
    uint8_t valueBuf[4];

    Protocol_PutU32(valueBuf, m.timestamp);
    Protocol_EncodeTLV(PROTO_FIELD_TIMESTAMP, valueBuf, 4, measurement + measurementLen, (uint16_t)(sizeof(measurement) - measurementLen), &written);
    measurementLen = (uint16_t)(measurementLen + written);

    Protocol_PutU16(valueBuf, (uint16_t)m.temperature);
    Protocol_EncodeTLV(PROTO_FIELD_TEMPERATURE, valueBuf, 2, measurement + measurementLen, (uint16_t)(sizeof(measurement) - measurementLen), &written);
    measurementLen = (uint16_t)(measurementLen + written);

    valueBuf[0] = m.humidity;
    Protocol_EncodeTLV(PROTO_FIELD_HUMIDITY, valueBuf, 1, measurement + measurementLen, (uint16_t)(sizeof(measurement) - measurementLen), &written);
    measurementLen = (uint16_t)(measurementLen + written);

    Protocol_PutU16(valueBuf, m.light);
    Protocol_EncodeTLV(PROTO_FIELD_LIGHT, valueBuf, 2, measurement + measurementLen, (uint16_t)(sizeof(measurement) - measurementLen), &written);
    measurementLen = (uint16_t)(measurementLen + written);

    Protocol_PutU16(valueBuf, m.batteryVoltage);
    Protocol_EncodeTLV(PROTO_FIELD_BATTERY_VOLTAGE, valueBuf, 2, measurement + measurementLen, (uint16_t)(sizeof(measurement) - measurementLen), &written);
    measurementLen = (uint16_t)(measurementLen + written);

    valueBuf[0] = m.mode;
    Protocol_EncodeTLV(PROTO_FIELD_MODE, valueBuf, 1, measurement + measurementLen, (uint16_t)(sizeof(measurement) - measurementLen), &written);
    measurementLen = (uint16_t)(measurementLen + written);

    uint8_t recordBuf[48];
    uint16_t recordLen;
    Protocol_EncodeTLV(PROTO_FIELD_MEASUREMENT_RECORD, measurement, measurementLen, recordBuf, sizeof(recordBuf), &recordLen);

    out.insert(out.end(), recordBuf, recordBuf + recordLen);
}

/**
 * @brief Encodes one DcaEvent as an EVENT_RECORD TLV (with a nested
 * MEASUREMENT_RECORD when hasMeasurement) and appends its bytes to out.
 */
static void AppendEventRecord(std::vector<uint8_t> &out, const DcaEvent &e)
{
    uint8_t eventBuf[80];
    uint16_t eventLen = 0, written;
    uint8_t valueBuf[4];

    Protocol_PutU32(valueBuf, e.timestamp);
    Protocol_EncodeTLV(PROTO_FIELD_TIMESTAMP, valueBuf, 4, eventBuf + eventLen, (uint16_t)(sizeof(eventBuf) - eventLen), &written);
    eventLen = (uint16_t)(eventLen + written);

    valueBuf[0] = e.eventType;
    Protocol_EncodeTLV(PROTO_FIELD_EVENT_TYPE, valueBuf, 1, eventBuf + eventLen, (uint16_t)(sizeof(eventBuf) - eventLen), &written);
    eventLen = (uint16_t)(eventLen + written);

    valueBuf[0] = e.eventSource;
    Protocol_EncodeTLV(PROTO_FIELD_EVENT_SOURCE, valueBuf, 1, eventBuf + eventLen, (uint16_t)(sizeof(eventBuf) - eventLen), &written);
    eventLen = (uint16_t)(eventLen + written);

    if (e.hasMeasurement) {
        uint8_t measurement[40];
        uint16_t measurementLen = 0, mwritten;

        Protocol_PutU32(valueBuf, e.measurement.timestamp);
        Protocol_EncodeTLV(PROTO_FIELD_TIMESTAMP, valueBuf, 4, measurement + measurementLen, (uint16_t)(sizeof(measurement) - measurementLen), &mwritten);
        measurementLen = (uint16_t)(measurementLen + mwritten);

        Protocol_PutU16(valueBuf, (uint16_t)e.measurement.temperature);
        Protocol_EncodeTLV(PROTO_FIELD_TEMPERATURE, valueBuf, 2, measurement + measurementLen, (uint16_t)(sizeof(measurement) - measurementLen), &mwritten);
        measurementLen = (uint16_t)(measurementLen + mwritten);

        valueBuf[0] = e.measurement.humidity;
        Protocol_EncodeTLV(PROTO_FIELD_HUMIDITY, valueBuf, 1, measurement + measurementLen, (uint16_t)(sizeof(measurement) - measurementLen), &mwritten);
        measurementLen = (uint16_t)(measurementLen + mwritten);

        Protocol_PutU16(valueBuf, e.measurement.light);
        Protocol_EncodeTLV(PROTO_FIELD_LIGHT, valueBuf, 2, measurement + measurementLen, (uint16_t)(sizeof(measurement) - measurementLen), &mwritten);
        measurementLen = (uint16_t)(measurementLen + mwritten);

        Protocol_PutU16(valueBuf, e.measurement.batteryVoltage);
        Protocol_EncodeTLV(PROTO_FIELD_BATTERY_VOLTAGE, valueBuf, 2, measurement + measurementLen, (uint16_t)(sizeof(measurement) - measurementLen), &mwritten);
        measurementLen = (uint16_t)(measurementLen + mwritten);

        valueBuf[0] = e.measurement.mode;
        Protocol_EncodeTLV(PROTO_FIELD_MODE, valueBuf, 1, measurement + measurementLen, (uint16_t)(sizeof(measurement) - measurementLen), &mwritten);
        measurementLen = (uint16_t)(measurementLen + mwritten);

        Protocol_EncodeTLV(PROTO_FIELD_MEASUREMENT_RECORD, measurement, measurementLen, eventBuf + eventLen, (uint16_t)(sizeof(eventBuf) - eventLen), &written);
        eventLen = (uint16_t)(eventLen + written);
    }

    uint8_t recordBuf[96];
    uint16_t recordLen;
    Protocol_EncodeTLV(PROTO_FIELD_EVENT_RECORD, eventBuf, eventLen, recordBuf, sizeof(recordBuf), &recordLen);

    out.insert(out.end(), recordBuf, recordBuf + recordLen);
}

/**
 * @brief Parses SUBMARINE_ID + TIME_RANGE_START/END out of a GS request's
 * Value. Returns false (and sends INTERNAL_ERROR) if any are missing.
 */
static bool ParseGsRangeRequest(const uint8_t *value, uint16_t valueLen,
                                 std::string &outSubmarineId, uint32_t &outStart, uint32_t &outEnd)
{
    const uint8_t *field;
    uint16_t fieldLen;

    if (Protocol_FindField(value, valueLen, PROTO_FIELD_SUBMARINE_ID, &field, &fieldLen) != PROTO_OK) return false;
    outSubmarineId.assign(reinterpret_cast<const char *>(field), fieldLen);

    if (Protocol_FindField(value, valueLen, PROTO_FIELD_TIME_RANGE_START, &field, &fieldLen) != PROTO_OK || fieldLen < 4) return false;
    Protocol_GetU32(field, &outStart);

    if (Protocol_FindField(value, valueLen, PROTO_FIELD_TIME_RANGE_END, &field, &fieldLen) != PROTO_OK || fieldLen < 4) return false;
    Protocol_GetU32(field, &outEnd);

    return true;
}

/**
 * @brief Handles one GS_GET_LOG_REQ: queries DCA, replies with
 * GS_GET_LOG_RESP.
 */
static void CC_GsLink_HandleGetLog(int clientFd, const uint8_t *value, uint16_t valueLen)
{
    std::string submarineId;
    uint32_t start, end;

    if (!ParseGsRangeRequest(value, valueLen, submarineId, start, end)) {
        uint8_t statusFieldBuf[1] = { (uint8_t)PROTO_STATUS_INTERNAL_ERROR };
        uint8_t statusField[4];
        uint16_t statusFieldLen;
        Protocol_EncodeTLV(PROTO_FIELD_STATUS, statusFieldBuf, 1, statusField, sizeof(statusField), &statusFieldLen);
        std::vector<uint8_t> payload(statusField, statusField + statusFieldLen);
        GsSendRaw(clientFd, WrapMessage(PROTO_TAG_GS_GET_LOG_RESP, payload));
        return;
    }

    std::vector<DcaMeasurement> records = DCA_QueryMeasurements(submarineId, start, end);

    uint8_t statusFieldBuf[1] = { (uint8_t)(records.empty() ? PROTO_STATUS_NO_DATA_FOUND : PROTO_STATUS_SUCCESS) };
    uint8_t statusField[4];
    uint16_t statusFieldLen;
    Protocol_EncodeTLV(PROTO_FIELD_STATUS, statusFieldBuf, 1, statusField, sizeof(statusField), &statusFieldLen);
    std::vector<uint8_t> payload(statusField, statusField + statusFieldLen);

    for (const auto &m : records) {
        if (payload.size() > GS_RESP_MAX_PAYLOAD) break;
        AppendMeasurementRecord(payload, m);
    }

    GsSendRaw(clientFd, WrapMessage(PROTO_TAG_GS_GET_LOG_RESP, payload));
}

/**
 * @brief Handles one GS_GET_EVENTS_REQ: queries DCA, replies with
 * GS_GET_EVENTS_RESP.
 */
static void CC_GsLink_HandleGetEvents(int clientFd, const uint8_t *value, uint16_t valueLen)
{
    std::string submarineId;
    uint32_t start, end;

    if (!ParseGsRangeRequest(value, valueLen, submarineId, start, end)) {
        uint8_t statusFieldBuf[1] = { (uint8_t)PROTO_STATUS_INTERNAL_ERROR };
        uint8_t statusField[4];
        uint16_t statusFieldLen;
        Protocol_EncodeTLV(PROTO_FIELD_STATUS, statusFieldBuf, 1, statusField, sizeof(statusField), &statusFieldLen);
        std::vector<uint8_t> payload(statusField, statusField + statusFieldLen);
        GsSendRaw(clientFd, WrapMessage(PROTO_TAG_GS_GET_EVENTS_RESP, payload));
        return;
    }

    std::vector<DcaEvent> records = DCA_QueryEvents(submarineId, start, end);

    uint8_t statusFieldBuf[1] = { (uint8_t)(records.empty() ? PROTO_STATUS_NO_DATA_FOUND : PROTO_STATUS_SUCCESS) };
    uint8_t statusField[4];
    uint16_t statusFieldLen;
    Protocol_EncodeTLV(PROTO_FIELD_STATUS, statusFieldBuf, 1, statusField, sizeof(statusField), &statusFieldLen);
    std::vector<uint8_t> payload(statusField, statusField + statusFieldLen);

    for (const auto &e : records) {
        if (payload.size() > GS_RESP_MAX_PAYLOAD) break;
        AppendEventRecord(payload, e);
    }

    GsSendRaw(clientFd, WrapMessage(PROTO_TAG_GS_GET_EVENTS_RESP, payload));
}

/**
 * @brief Dispatches one request from an already-connected GS client.
 * @return false if the client disconnected or sent malformed data (caller
 * should close the connection); true to keep serving this client.
 */
static bool CC_GsLink_Dispatch(int clientFd)
{
    uint8_t storage[512];
    uint8_t tag;
    const uint8_t *value;
    uint16_t valueLen;

    if (!GsRecvMessage(clientFd, &tag, &value, &valueLen, storage, sizeof(storage))) {
        return false;
    }

    if (tag == PROTO_TAG_GS_GET_LOG_REQ) {
        CC_GsLink_HandleGetLog(clientFd, value, valueLen);
    } else if (tag == PROTO_TAG_GS_GET_EVENTS_REQ) {
        CC_GsLink_HandleGetEvents(clientFd, value, valueLen);
    } else {
        fprintf(stderr, "central_computer: GS sent unhandled tag 0x%02X\n", tag);
    }

    return true;
}

/**
 * @brief Runs forever on its own thread (Phase 15): binds/listens once,
 * then repeatedly accepts a GS client, performs the handshake, serves
 * GS_GET_LOG_REQ/GS_GET_EVENTS_REQ (via CC_GsLink_Dispatch) until that
 * client disconnects, and goes back to accepting the next one. Runs
 * concurrently with the main thread's LNC handling — see dca.cpp's mutex
 * for the resulting thread-safety requirement on DCA's file operations.
 * @param port Port to listen on.
 */
static void CC_GsLink_Run(uint16_t port)
{
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        fprintf(stderr, "central_computer: GS listen socket() failed: %s\n", strerror(errno));
        return;
    }

    int reuse = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); /* GS is a different machine in general, unlike the loopback-only lnc_bridge link */
    addr.sin_port = htons(port);

    if (bind(listenFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "central_computer: GS listen bind() failed on port %u: %s\n", port, strerror(errno));
        close(listenFd);
        return;
    }

    if (listen(listenFd, 1) < 0) {
        fprintf(stderr, "central_computer: GS listen() failed: %s\n", strerror(errno));
        close(listenFd);
        return;
    }

    printf("central_computer: GS link listening on port %u, waiting for Ground Station...\n", port);

    for (;;) {
        int clientFd = accept(listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            fprintf(stderr, "central_computer: GS accept() failed: %s\n", strerror(errno));
            continue; /* keep listening rather than giving up entirely */
        }
        printf("central_computer: Ground Station connected\n");

        uint8_t prefix[4];
        if (recv(clientFd, prefix, sizeof(prefix), MSG_WAITALL) != sizeof(prefix)) {
            fprintf(stderr, "central_computer: GS handshake — failed to read greeting length\n");
            close(clientFd);
            continue;
        }
        uint32_t greetingLen;
        Protocol_GetU32(prefix, &greetingLen);

        char greeting[64] = {0};
        if (greetingLen >= sizeof(greeting) || recv(clientFd, greeting, greetingLen, MSG_WAITALL) != (ssize_t)greetingLen) {
            fprintf(stderr, "central_computer: GS handshake — failed to read greeting\n");
            close(clientFd);
            continue;
        }
        printf("central_computer: received GS greeting: %s\n", greeting);

        bool greetingOk = (strcmp(greeting, GS_HANDSHAKE_GREETING) == 0);
        printf("Phase 9 test: %s\n", greetingOk ? "PASSED" : "FAILED");

        const char *reply = GS_HANDSHAKE_REPLY;
        uint16_t replyLen = (uint16_t)strlen(reply);
        Protocol_PutU32(prefix, replyLen);
        send(clientFd, prefix, sizeof(prefix), 0);
        send(clientFd, reply, replyLen, 0);

        if (greetingOk) {
            while (CC_GsLink_Dispatch(clientFd)) {
                /* keep serving requests until the GS disconnects */
            }
            printf("central_computer: Ground Station disconnected\n");
        }

        close(clientFd);
        /* loop back and accept the next GS client — listenFd stays open */
    }
}

static void CcCore_PrintEventReport(const uint8_t *value, uint16_t len)
{
    const uint8_t *field;
    uint16_t fieldLen;

    uint32_t timestamp = 0;
    if (Protocol_FindField(value, len, PROTO_FIELD_TIMESTAMP, &field, &fieldLen) == PROTO_OK) {
        Protocol_GetU32(field, &timestamp);
    }

    uint8_t eventType = 0, eventSource = 0;
    if (Protocol_FindField(value, len, PROTO_FIELD_EVENT_TYPE, &field, &fieldLen) == PROTO_OK) {
        eventType = field[0];
    }
    if (Protocol_FindField(value, len, PROTO_FIELD_EVENT_SOURCE, &field, &fieldLen) == PROTO_OK) {
        eventSource = field[0];
    }

    char timeStr[32];
    FormatUnixTime(timestamp, timeStr, sizeof(timeStr));

    printf("EVENT_REPORT: time=%s type=%u source=%u", timeStr, eventType, eventSource);

    if (Protocol_FindField(value, len, PROTO_FIELD_WD_RESET_FLAG, &field, &fieldLen) == PROTO_OK) {
        printf(" wd_reset=%u", field[0]);
    }

    const uint8_t *measurement;
    uint16_t measurementLen;
    if (Protocol_FindField(value, len, PROTO_FIELD_MEASUREMENT_RECORD, &measurement, &measurementLen) == PROTO_OK) {
        int16_t temperature = 0;
        uint8_t humidity = 0, mode = 0;
        uint16_t light = 0, battery = 0;

        if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_TEMPERATURE, &field, &fieldLen) == PROTO_OK) {
            uint16_t raw; Protocol_GetU16(field, &raw); temperature = (int16_t)raw;
        }
        if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_HUMIDITY, &field, &fieldLen) == PROTO_OK) {
            humidity = field[0];
        }
        if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_LIGHT, &field, &fieldLen) == PROTO_OK) {
            Protocol_GetU16(field, &light);
        }
        if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_BATTERY_VOLTAGE, &field, &fieldLen) == PROTO_OK) {
            Protocol_GetU16(field, &battery);
        }
        if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_MODE, &field, &fieldLen) == PROTO_OK) {
            mode = field[0];
        }

        DCA_StoreEvent(SUBMARINE_ID, timestamp, eventType, eventSource, true, temperature, humidity, light, battery, mode);

        printf(" | temp=%.1fC humidity=%u%% light=%u%% battery=%u%% mode=%u",
               temperature / 10.0, humidity, (unsigned)(light * 100 / 4095),
               (unsigned)(battery * 100 / 3300), mode);
    } else {
        DCA_StoreEvent(SUBMARINE_ID, timestamp, eventType, eventSource, false, 0, 0, 0, 0, 0);
    }
    printf("\n");
}



int main()
{
    /* Phase 15: GS serving runs continuously on its own thread, concurrent
       with the LNC handling below — see CC_GsLink_Run's own comment and
       dca.cpp's mutex for why this is safe. */
    std::thread gsThread(CC_GsLink_Run, GS_TCP_PORT);
    gsThread.detach();

    bool firstConnection = true;

    /* Phase 15: reconnect loop — a dropped LNC link (lnc_bridge restarting,
       or the LNC itself rebooting) no longer ends the program. It retries
       connecting until it succeeds, then resumes normal operation. */
    for (;;) {
        int fd = CcCore_LncConnect();
        if (fd < 0) {
            fprintf(stderr, "central_computer: no lnc_bridge connection, retrying in 3s...\n");
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }
        printf("central_computer: connected to lnc_bridge on 127.0.0.1:%d\n", BRIDGE_TCP_PORT);

        /* --- Get/Set time round-trip, re-synced on EVERY (re)connection ---
           Not just the first: this is what actually fixes the gap found
           during Watchdog testing — if the LNC itself rebooted (watchdog-
           triggered or otherwise) while this process kept running, its
           clock reverts to the fake boot baseline and needs re-syncing,
           the same as a fresh first connection would. */
        uint32_t initialTime = 0;
        bool ok = CcCore_GetTime(fd, &initialTime);
        if (ok) printf("central_computer: initial LNC time = %u\n", (unsigned)initialTime);

        uint32_t newTime = (uint32_t)time(nullptr); /* sync the LNC's RTC to this machine's real time */
        ProtoStatus_t setStatus = PROTO_STATUS_INTERNAL_ERROR;
        if (ok) {
            ok = CcCore_SetRtc(fd, newTime, &setStatus) && (setStatus == PROTO_STATUS_SUCCESS);
            if (ok) printf("central_computer: SET_RTC_REQ acknowledged, status=SUCCESS\n");
        }

        uint32_t confirmedTime = 0;
        if (ok) {
            ok = CcCore_GetTime(fd, &confirmedTime);
            if (ok) printf("central_computer: LNC time after set = %u\n", (unsigned)confirmedTime);
        }

        /* Exact equality would be flaky now that newTime is a real,
           continuously-advancing clock value rather than an artificial
           +1000s jump — a second can genuinely tick over between the SET
           and this GET due to normal round-trip latency. */
        bool match = ok && (confirmedTime >= newTime) && (confirmedTime <= newTime + 2);
        printf("Phase 8 test: %s\n", match ? "PASSED" : "FAILED");

        if (firstConnection) {
            /* --- Phase 12/13 one-time demo tests — proven once at first
               connection, not repeated on every reconnect (see the
               option-1-for-now decision: these stay as startup self-tests,
               to be cleaned up properly, if wanted, in Phase 17). --- */
            ProtoStatus_t setConfigStatus = PROTO_STATUS_INTERNAL_ERROR;
            bool configOk = CcCore_SetBatteryWarningMin(fd, 1500, &setConfigStatus) && (setConfigStatus == PROTO_STATUS_SUCCESS);
            printf("Phase 12 test: SET_BATTERY_WARNING_MIN %s\n", configOk ? "PASSED" : "FAILED");

            uint32_t queryStart = (newTime > 60) ? (newTime - 60) : 0;
            uint32_t queryEnd = confirmedTime + 60;
            CcCore_GetMeasurements(fd, queryStart, queryEnd);

            uint32_t eventsQueryStart = (newTime > 10) ? (newTime - 10) : 0;
            uint32_t eventsQueryEnd = confirmedTime + 10;
            CcCore_GetEvents(fd, eventsQueryStart, eventsQueryEnd);

            firstConnection = false;
        }

        /* --- Streaming loop: print/store KEEP_ALIVE/EVENT_REPORT until the
           link drops, then fall through to the outer loop and reconnect. --- */
        for (;;) {
            uint8_t storage[256];
            uint8_t tag;
            const uint8_t *value;
            uint16_t valueLen;

            if (!CcCore_LncRecvMessage(fd, &tag, &value, &valueLen, storage, sizeof(storage))) {
                fprintf(stderr, "central_computer: lnc_bridge disconnected\n");
                break;
            }
            if (tag == PROTO_TAG_EVENT_REPORT) {
                CcCore_PrintEventReport(value, valueLen);
            } else {
                printf("central_computer: received unhandled tag 0x%02X\n", tag);
            }
        }

        close(fd);
        fprintf(stderr, "central_computer: attempting to reconnect...\n");
    }
}
