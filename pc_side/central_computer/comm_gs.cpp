/*
 * comm_gs.cpp — see comm_gs.h.
 */

#include "comm_gs.h"
#include "data_collection.h"
#include "protocol.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define GS_HANDSHAKE_GREETING "GS_HELLO"
#define GS_HANDSHAKE_REPLY    "CC_HELLO_ACK"

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

void CC_GsLink_Run(uint16_t port)
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
        if (!greetingOk) {
            fprintf(stderr, "central_computer: GS handshake failed (unexpected greeting)\n");
        }

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
