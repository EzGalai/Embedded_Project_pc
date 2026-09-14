/**
 * @file main.cpp
 * @brief central_computer core — Phase 7 (KEEP_ALIVE) + Phase 8 (Get/Set
 * time round-trip). See PROJECT_PLAN.md §5.1, §5.3, §6 Phase 8.
 */

#include "protocol.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BRIDGE_TCP_PORT 5100
#define GS_TCP_PORT 9000
#define GS_HANDSHAKE_GREETING "GS_HELLO"
#define GS_HANDSHAKE_REPLY    "CC_HELLO_ACK"

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
    gmtime_r(&t, &tmVal);
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

/**
 * @brief Listens for and serves one Ground Station connection: accepts a
 * client, reads its length-prefixed greeting, and replies with a fixed
 * length-prefixed acknowledgment. Blocking, single connection — this phase
 * proves the link mechanics only, not concurrent operation alongside the
 * LNC-facing link (PROJECT_PLAN.md §6 Phase 9).
 * @param port Port to listen on.
 * @return true if a client connected and sent the expected greeting.
 */
static bool CC_GsLink_Listen(uint16_t port)
{
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        fprintf(stderr, "central_computer: GS listen socket() failed: %s\n", strerror(errno));
        return false;
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
        return false;
    }

    if (listen(listenFd, 1) < 0) {
        fprintf(stderr, "central_computer: GS listen() failed: %s\n", strerror(errno));
        close(listenFd);
        return false;
    }

    printf("central_computer: GS link listening on port %u, waiting for Ground Station...\n", port);

    int clientFd = accept(listenFd, nullptr, nullptr);
    if (clientFd < 0) {
        fprintf(stderr, "central_computer: GS accept() failed: %s\n", strerror(errno));
        close(listenFd);
        return false;
    }
    printf("central_computer: Ground Station connected\n");

    uint8_t prefix[4];
    if (recv(clientFd, prefix, sizeof(prefix), MSG_WAITALL) != sizeof(prefix)) {
        fprintf(stderr, "central_computer: GS handshake — failed to read greeting length\n");
        close(clientFd);
        close(listenFd);
        return false;
    }
    uint32_t greetingLen;
    Protocol_GetU32(prefix, &greetingLen);

    char greeting[64] = {0};
    if (greetingLen >= sizeof(greeting) || recv(clientFd, greeting, greetingLen, MSG_WAITALL) != (ssize_t)greetingLen) {
        fprintf(stderr, "central_computer: GS handshake — failed to read greeting\n");
        close(clientFd);
        close(listenFd);
        return false;
    }
    printf("central_computer: received GS greeting: %s\n", greeting);

    bool greetingOk = (strcmp(greeting, GS_HANDSHAKE_GREETING) == 0);

    const char *reply = GS_HANDSHAKE_REPLY;
    uint16_t replyLen = (uint16_t)strlen(reply);
    Protocol_PutU32(prefix, replyLen);
    send(clientFd, prefix, sizeof(prefix), 0);
    send(clientFd, reply, replyLen, 0);

    close(clientFd);
    close(listenFd);

    return greetingOk;
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

        printf(" | temp=%.1fC humidity=%u%% light=%u%% battery=%u%% mode=%u",
               temperature / 10.0, humidity, (unsigned)(light * 100 / 4095),
               (unsigned)(battery * 100 / 3300), mode);
    }
    printf("\n");
}



int main()
{
    int fd = CcCore_LncConnect();
    bool lncConnected = (fd >= 0);

    if (lncConnected) {
        printf("central_computer: connected to lnc_bridge on 127.0.0.1:%d\n", BRIDGE_TCP_PORT);

        /* --- Phase 8: Get/Set time round-trip test --- */
        uint32_t initialTime = 0;
        bool ok = CcCore_GetTime(fd, &initialTime);
        if (ok) printf("central_computer: initial LNC time = %u\n", (unsigned)initialTime);

        uint32_t newTime = initialTime + 1000;
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

        bool match = ok && (confirmedTime == newTime);
        printf("Phase 8 test: %s\n", match ? "PASSED" : "FAILED");

        /* --- Phase 12: Config SET round-trip test --- */
        ProtoStatus_t setConfigStatus = PROTO_STATUS_INTERNAL_ERROR;
        bool configOk = CcCore_SetBatteryWarningMin(fd, 1500, &setConfigStatus) && (setConfigStatus == PROTO_STATUS_SUCCESS);
        printf("Phase 12 test: SET_BATTERY_WARNING_MIN %s\n", configOk ? "PASSED" : "FAILED");

        /* --- Phase 13: GET_MEASUREMENTS_REQ/RESP retrieval test ---
           The window must span BOTH initialTime and confirmedTime, not
           just sit near confirmedTime — Phase 8's own test jumps the RTC
           forward by 1000s (newTime = initialTime + 1000), so on a fresh
           boot (little real time elapsed yet) all existing log data was
           written using the PRE-jump time, while confirmedTime is already
           1000s past that. A window only around confirmedTime misses it
           entirely. 60s of margin on each end keeps it from also sweeping
           in unrelated older history from earlier test runs today. */
        uint32_t queryStart = (initialTime > 60) ? (initialTime - 60) : 0;
        uint32_t queryEnd = confirmedTime + 60;
        CcCore_GetMeasurements(fd, queryStart, queryEnd);

        /* --- Phase 13: GET_EVENTS_REQ/RESP retrieval test ---
           Same start-from-initialTime reasoning as above, but a smaller
           end-margin (+10s not +60s) — Object Detection has been firing
           every ~200ms poll cycle, so a wide window can mean 100+ events
           to page through. */
        uint32_t eventsQueryStart = (initialTime > 10) ? (initialTime - 10) : 0;
        uint32_t eventsQueryEnd = confirmedTime + 10;
        CcCore_GetEvents(fd, eventsQueryStart, eventsQueryEnd);
    }
    else {
        printf("central_computer: no lnc_bridge connection — continuing without the LNC link\n");
    }

    /* --- Phase 9: Ground Station link test (independent of the LNC link) --- */
    bool gsOk = CC_GsLink_Listen(GS_TCP_PORT);
    printf("Phase 9 test: %s\n", gsOk ? "PASSED" : "FAILED");

    if (lncConnected) {
        /* --- Phase 7 behavior continues: print any further KEEP_ALIVEs forever --- */
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
    }

    return 0;
}
