/**
 * @file main.cpp
 * @brief central_computer core — Phase 7 (KEEP_ALIVE) + Phase 8 (Get/Set
 * time round-trip). See PROJECT_PLAN.md §5.1, §5.3, §6 Phase 8.
 */

#include "protocol.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BRIDGE_TCP_PORT 5100
#define GS_TCP_PORT 9000
#define GS_HANDSHAKE_GREETING "GS_HELLO"
#define GS_HANDSHAKE_REPLY    "CC_HELLO_ACK"

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

    const uint8_t *measurement;
    uint16_t measurementLen;
    if (Protocol_FindField(value, len, PROTO_FIELD_MEASUREMENT_RECORD, &measurement, &measurementLen) != PROTO_OK) {
        printf("KEEP_ALIVE: timestamp=%u mode=%u (no MEASUREMENT_RECORD found)\n", (unsigned)timestamp, mode);
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

    printf("KEEP_ALIVE: timestamp=%u mode=%u | temp=%.1fC humidity=%u%% light=%u%% battery=%u%%\n",
       (unsigned)timestamp, mode, temperature / 10.0, humidity,
       (unsigned)(light * 100 / 4095), (unsigned)(battery * 100 / 3300));

}

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

    printf("EVENT_REPORT: timestamp=%u type=%u source=%u", (unsigned)timestamp, eventType, eventSource);

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
    } else {
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
