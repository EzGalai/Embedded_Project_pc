/*
 * lnc_link_client.cpp — see lnc_link_client.h.
 */

#include "lnc_link_client.h"
#include "log.h"
#include "protocol.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void FormatUnixTime(uint32_t timestamp, char *outBuf, size_t bufSize)
{
    time_t t = (time_t)timestamp;
    struct tm tmVal;
    localtime_r(&t, &tmVal);
    strftime(outBuf, bufSize, "%Y-%m-%d %H:%M:%S", &tmVal);
}

int CcCore_LncConnect(const char *host, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "central_computer: socket() failed: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "central_computer: invalid lnc_bridge host '%s'\n", host);
        close(fd);
        return -1;
    }

    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "central_computer: connect() to lnc_bridge failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

uint16_t CcCore_LncRecv(int fd, uint8_t *outBuf, uint16_t maxLen)
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

void CcCore_LncSend(int fd, uint8_t tag, const uint8_t *value, uint16_t valueLen)
{
    uint8_t message[32];
    uint16_t messageLen;
    Protocol_EncodeTLV(tag, value, valueLen, message, sizeof(message), &messageLen);

    uint8_t prefix[4];
    Protocol_PutU32(prefix, messageLen);
    send(fd, prefix, sizeof(prefix), 0);
    send(fd, message, messageLen, 0);
}

bool CcCore_LncRecvMessage(int fd, uint8_t *outTag, const uint8_t **outValue, uint16_t *outValueLen,
                           uint8_t *storage, uint16_t storageCap, const std::string &submarineId)
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
            CcCore_PrintKeepAlive(*outValue, *outValueLen, submarineId);
            continue; /* not what we're waiting for — keep listening */
        }
        if (*outTag == PROTO_TAG_EVENT_REPORT) {
            CcCore_PrintEventReport(*outValue, *outValueLen, submarineId);
            continue; /* not what we're waiting for — keep listening */
        }

        return true;
    }
}
