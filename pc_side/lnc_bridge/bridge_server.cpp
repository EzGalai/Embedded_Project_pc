/**
 * @file bridge_server.cpp
 * @brief Implementation of the lnc_bridge <-> central_computer TCP link (see bridge_server.h).
 *
 * Two things worth knowing going in:
 * - Every socket here is non-blocking (O_NONBLOCK), so accept()/recv()/send() never
 *   freeze the process waiting on the network — a call with nothing to do returns -1
 *   with errno == EAGAIN instead, which every function below treats as "try again next
 *   loop iteration," not an error.
 * - TCP is a byte stream, not a message stream: the client's bytes can arrive split
 *   across multiple recv() calls or coalesced with the next message's bytes. g_recvBuf
 *   accumulates raw bytes across calls until a full [4-byte length][payload] frame is
 *   present — same problem Frame_Decode() solves for UART, simpler solution here since
 *   TCP is already reliable.
 */

#include "bridge_server.h"
#include "protocol.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdio>

/* Room for the 4-byte length prefix plus one payload. Sized for Phase 6's small
   test payloads; revisit if a later phase's core-side messages need more. */
#define BRIDGE_RECV_BUF_SIZE 512

static int g_listenFd = -1;         /* bound+listening socket, -1 if not up */
static int g_clientFd = -1;         /* the one accepted client, -1 if none */
static uint8_t g_recvBuf[BRIDGE_RECV_BUF_SIZE];
static uint16_t g_recvLen = 0;      /* bytes currently accumulated in g_recvBuf */

bool BridgeServer_Init(uint16_t port)
{
    g_listenFd = socket(AF_INET, SOCK_STREAM, 0); /* IPv4 TCP */
    if (g_listenFd < 0) {
        fprintf(stderr, "BridgeServer_Init: socket() failed: %s\n", strerror(errno));
        return false;
    }

    /* Lets a restarted lnc_bridge rebind immediately instead of hitting
       "Address already in use" during the kernel's post-close TIME_WAIT. */
    int reuse = 1;
    setsockopt(g_listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* INADDR_LOOPBACK (127.0.0.1), not INADDR_ANY — only reachable from this
       machine, per this link's same-machine design. */
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(g_listenFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "BridgeServer_Init: bind() failed on port %u: %s\n", port, strerror(errno));
        close(g_listenFd);
        g_listenFd = -1;
        return false;
    }

    /* Backlog of 1: we only ever want a single client. */
    if (listen(g_listenFd, 1) < 0) {
        fprintf(stderr, "BridgeServer_Init: listen() failed: %s\n", strerror(errno));
        close(g_listenFd);
        g_listenFd = -1;
        return false;
    }

    int flags = fcntl(g_listenFd, F_GETFL, 0);
    fcntl(g_listenFd, F_SETFL, flags | O_NONBLOCK);

    g_clientFd = -1;
    g_recvLen = 0;
    return true;
}

bool BridgeServer_Accept(void)
{
    if (g_clientFd != -1) {
        return true; /* already connected */
    }
    if (g_listenFd < 0) {
        return false;
    }

    int fd = accept(g_listenFd, nullptr, nullptr);
    if (fd < 0) {
        return false; /* EAGAIN/EWOULDBLOCK: nobody waiting — not an error */
    }

    /* The accepted fd is new and does not inherit O_NONBLOCK from the listening socket. */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    g_clientFd = fd;
    g_recvLen = 0;
    fprintf(stderr, "BridgeServer: client connected\n");
    return true;
}

void BridgeServer_Send(const uint8_t *data, uint16_t len)
{
    if (g_clientFd < 0) {
        return;
    }

    uint8_t prefix[4];
    Protocol_PutU32(prefix, len);

    /* Phase 6 scope: no partial-send retry loop — fine for these small test
       payloads; a production version would loop until all bytes are confirmed sent. */
    if (send(g_clientFd, prefix, sizeof(prefix), 0) < 0 ||
        send(g_clientFd, data, len, 0) < 0) {
        fprintf(stderr, "BridgeServer_Send: client disconnected: %s\n", strerror(errno));
        close(g_clientFd);
        g_clientFd = -1;
    }
}

uint16_t BridgeServer_TryRecv(uint8_t *outBuf, uint16_t maxLen)
{
    if (g_clientFd < 0) {
        return 0;
    }

    if (g_recvLen < sizeof(g_recvBuf)) {
        ssize_t n = recv(g_clientFd, g_recvBuf + g_recvLen, sizeof(g_recvBuf) - g_recvLen, 0);
        if (n > 0) {
            g_recvLen = (uint16_t)(g_recvLen + n);
        } else if (n == 0) {
            /* 0 means clean shutdown by the peer, distinct from -1 (nothing available yet). */
            fprintf(stderr, "BridgeServer: client disconnected\n");
            close(g_clientFd);
            g_clientFd = -1;
            g_recvLen = 0;
            return 0;
        }
        /* n < 0: EAGAIN or a transient error — nothing new, fall through to check
           whatever's already accumulated. */
    }

    if (g_recvLen < 4) {
        return 0; /* length prefix hasn't fully arrived yet */
    }

    uint32_t payloadLen;
    Protocol_GetU32(g_recvBuf, &payloadLen);

    uint32_t frameLen = 4 + payloadLen;
    if (g_recvLen < frameLen) {
        return 0; /* prefix is here, payload isn't fully here yet */
    }

    uint16_t copyLen = (payloadLen < maxLen) ? (uint16_t)payloadLen : maxLen;
    if (payloadLen > maxLen) {
        fprintf(stderr, "BridgeServer_TryRecv: payload (%u bytes) truncated to %u\n",
                (unsigned)payloadLen, maxLen);
    }
    memcpy(outBuf, g_recvBuf + 4, copyLen);

    /* Slide any leftover bytes (start of the next frame, if the client sent more
       than one message back-to-back) down to the front for the next call. */
    uint16_t remaining = (uint16_t)(g_recvLen - frameLen);
    memmove(g_recvBuf, g_recvBuf + frameLen, remaining);
    g_recvLen = remaining;

    return copyLen;
}

bool BridgeServer_IsConnected(void)
{
    return g_clientFd != -1;
}

void BridgeServer_Close(void)
{
    if (g_clientFd >= 0) {
        close(g_clientFd);
        g_clientFd = -1;
    }
    if (g_listenFd >= 0) {
        close(g_listenFd);
        g_listenFd = -1;
    }
    g_recvLen = 0;
}
