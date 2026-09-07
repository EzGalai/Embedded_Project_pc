/**
 * @file main.cpp
 * @brief central_computer core — Phase 7: connects to lnc_bridge over the
 * TCP bridge link and prints incoming KEEP_ALIVE messages (PROJECT_PLAN.md
 * §5.1, §6 Phase 7). First real central_computer code — replaces Phase 6's
 * one-shot stub_core_client test with a long-running connection that
 * actually decodes protocol messages.
 *
 * Blocking sockets throughout: nothing else for this process to do yet while
 * waiting for the next message, so lnc_bridge's non-blocking multiplexing
 * isn't needed here (that resurfaces once this process also serves the
 * Ground Station link, in a later phase).
 */

#include "protocol.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BRIDGE_TCP_PORT 5100

/**
 * @brief Connect to lnc_bridge's TCP server.
 * @return Connected socket fd, or -1 on failure (diagnostic printed to stderr).
 */
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

/**
 * @brief Block until one complete length-prefixed payload arrives from lnc_bridge.
 * @param fd Connected socket.
 * @param outBuf Destination buffer.
 * @param maxLen Capacity of outBuf.
 * @return Number of payload bytes received, or 0 on error/disconnect.
 */
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
 * @brief Decode a KEEP_ALIVE message's Value and print its fields.
 * @param value Pointer to KEEP_ALIVE's Value (its TLV payload, tag/length already stripped).
 * @param len Length of value.
 */
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

    printf("KEEP_ALIVE: timestamp=%u mode=%u | temp=%.1fC humidity=%u%% light=%u battery=%umV\n",
           (unsigned)timestamp, mode, temperature / 10.0, humidity, light, battery);
}

int main()
{
    int fd = CcCore_LncConnect();
    if (fd < 0) {
        return 1;
    }
    printf("central_computer: connected to lnc_bridge on 127.0.0.1:%d\n", BRIDGE_TCP_PORT);

    for (;;) {
        uint8_t payload[256];
        uint16_t payloadLen = CcCore_LncRecv(fd, payload, sizeof(payload));
        if (payloadLen == 0) {
            fprintf(stderr, "central_computer: lnc_bridge disconnected\n");
            break;
        }

        uint8_t tag;
        const uint8_t *value;
        uint16_t valueLen, consumed;
        if (Protocol_DecodeTLV(payload, payloadLen, &tag, &value, &valueLen, &consumed) != PROTO_OK) {
            fprintf(stderr, "central_computer: malformed message from lnc_bridge\n");
            continue;
        }

        if (tag == PROTO_TAG_KEEP_ALIVE) {
            CcCore_PrintKeepAlive(value, valueLen);
        } else {
            printf("central_computer: received unhandled tag 0x%02X\n", tag);
        }
    }

    close(fd);
    return 0;
}
