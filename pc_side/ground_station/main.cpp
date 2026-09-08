/**
 * @file main.cpp
 * @brief ground_station — Phase 9: connects to central_computer's GS-facing
 * link and performs the "trivial handshake" (PROJECT_PLAN.md §6 Phase 9).
 *
 * This handshake is deliberately outside the TLV protocol — no protocol.h
 * message exists for it, since the real GS_* messages (GS_GET_LOG_REQ etc.)
 * aren't wired until Phase 14. It only proves the CC<->GS Ethernet link
 * itself works, independent of the LNC link/bridge process.
 */

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define CC_GS_PORT 9000
#define GS_HANDSHAKE_GREETING "GS_HELLO"
#define GS_HANDSHAKE_REPLY    "CC_HELLO_ACK"

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

    close(fd);
    return match ? 0 : 1;
}
