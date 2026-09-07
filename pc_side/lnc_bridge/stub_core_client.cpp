/**
 * @file stub_core_client.cpp
 * @brief Phase 6 test client — stands in for central_computer core to exercise
 * the full round trip: this client -> lnc_bridge (TCP) -> LNC (UART) ->
 * Phase4EchoTask -> back.
 *
 * Uses a single blocking socket, unlike lnc_bridge's non-blocking design —
 * fine here since this is a one-shot test that's allowed to wait.
 */

#include "protocol.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BRIDGE_TCP_PORT 5100

int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "stub_core_client: socket() failed: %s\n", strerror(errno));
        return 1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(BRIDGE_TCP_PORT);

    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "stub_core_client: connect() failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("stub_core_client: connected to lnc_bridge on 127.0.0.1:%d\n", BRIDGE_TCP_PORT);

    /* Send one length-prefixed payload */
    const uint8_t payload[] = "PHASE6BRIDGE";
    uint16_t payloadLen = (uint16_t)(sizeof(payload) - 1); /* exclude trailing '\0' */

    uint8_t prefix[4];
    Protocol_PutU32(prefix, payloadLen);

    if (send(fd, prefix, sizeof(prefix), 0) < 0 || send(fd, payload, payloadLen, 0) < 0) {
        fprintf(stderr, "stub_core_client: send() failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("stub_core_client: sent payload: %s\n", payload);

    /* MSG_WAITALL: block until exactly this many bytes arrive (or error/close) */
    uint8_t rxPrefix[4];
    ssize_t n = recv(fd, rxPrefix, sizeof(rxPrefix), MSG_WAITALL);
    if (n != sizeof(rxPrefix)) {
        fprintf(stderr, "stub_core_client: failed to read response length prefix (n=%zd): %s\n",
                n, strerror(errno));
        close(fd);
        return 1;
    }

    uint32_t respLen;
    Protocol_GetU32(rxPrefix, &respLen);

    uint8_t rxPayload[256];
    if (respLen > sizeof(rxPayload)) {
        fprintf(stderr, "stub_core_client: response too large (%u bytes)\n", (unsigned)respLen);
        close(fd);
        return 1;
    }

    n = recv(fd, rxPayload, respLen, MSG_WAITALL);
    if (n != (ssize_t)respLen) {
        fprintf(stderr, "stub_core_client: failed to read response payload (n=%zd): %s\n",
                n, strerror(errno));
        close(fd);
        return 1;
    }

    bool match = (respLen == payloadLen) && (memcmp(rxPayload, payload, payloadLen) == 0);
    printf("stub_core_client: received %u bytes: %.*s\n", (unsigned)respLen, (int)respLen, rxPayload);
    printf("Phase 6 test: %s\n", match ? "PASSED" : "FAILED");

    close(fd);
    return match ? 0 : 1;
}
