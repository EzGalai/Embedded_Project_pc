/**
 * @file main.cpp
 * @brief lnc_bridge — standalone gateway process bridging the LNC's UART link
 * to the TCP link that central_computer core connects to (Phase 6, PROJECT_PLAN.md §4.11-4.13).
 *
 * Owns two independent byte streams and shuttles payloads between them:
 *   Uplink (LNC -> core):   Transport_Recv (UART) -> Frame_Decode -> BridgeServer_Send (TCP)
 *   Downlink (core -> LNC): BridgeServer_TryRecv (TCP) -> Frame_Encode -> Transport_Send (UART)
 *
 * Never parses a TLV tag or knows what a Management Command is — purely a
 * framer/deframer and forwarder, per §4.11.
 */

#include "transport.h"
#include "protocol.h"
#include "bridge_server.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

#define BRIDGE_TCP_PORT 5100

int main()
{
    Transport_Init();

    if (!BridgeServer_Init(BRIDGE_TCP_PORT)) {
        fprintf(stderr, "lnc_bridge: failed to start TCP server on port %d\n", BRIDGE_TCP_PORT);
        return 1;
    }
    printf("lnc_bridge: listening on 127.0.0.1:%d, waiting for central_computer core...\n", BRIDGE_TCP_PORT);

    uint8_t uartRxBuf[256];
    uint16_t uartRxLen = 0;

    for (;;) {
        BridgeServer_Accept(); /* no-op if already connected or nobody waiting */

        /* ---- Uplink: LNC -> core ---- */
        uint16_t n = Transport_Recv(uartRxBuf + uartRxLen, (uint16_t)(sizeof(uartRxBuf) - uartRxLen));
        uartRxLen = (uint16_t)(uartRxLen + n);

        if (uartRxLen > 0) {
            uint8_t payload[256];
            uint16_t payloadLen, consumed;
            ProtoResult_t r = Frame_Decode(uartRxBuf, uartRxLen, payload, sizeof(payload), &payloadLen, &consumed);

            if (r == PROTO_OK) {
                if (BridgeServer_IsConnected()) {
                    printf("lnc_bridge: uplink — forwarding %u byte payload to core\n", payloadLen);
                } else {
                    printf("lnc_bridge: uplink — no core connected, dropping %u byte payload\n", payloadLen);
                }
                BridgeServer_Send(payload, payloadLen);
                memmove(uartRxBuf, uartRxBuf + consumed, (size_t)(uartRxLen - consumed));
                uartRxLen = (uint16_t)(uartRxLen - consumed);
            } else if (r == PROTO_ERR_MALFORMED) {
                memmove(uartRxBuf, uartRxBuf + consumed, (size_t)(uartRxLen - consumed));
                uartRxLen = (uint16_t)(uartRxLen - consumed);
            } else if (r == PROTO_ERR_BUFFER_TOO_SMALL) {
                uartRxLen = 0; /* frame too large to recover — drop and resync on the next START */
            }
            /* PROTO_ERR_INCOMPLETE: leave uartRxBuf as-is, wait for more bytes next iteration */
        }

        /* ---- Downlink: core -> LNC ---- */
        uint8_t tcpPayload[256];
        uint16_t tcpPayloadLen = BridgeServer_TryRecv(tcpPayload, sizeof(tcpPayload));
        if (tcpPayloadLen > 0) {
            printf("lnc_bridge: downlink — forwarding %u byte payload to LNC\n", tcpPayloadLen);
            uint8_t framed[600]; /* worst-case byte-stuffed size for a 256-byte payload */
            uint16_t framedLen;
            Frame_Encode(tcpPayload, tcpPayloadLen, framed, sizeof(framed), &framedLen);
            Transport_Send(framed, framedLen);
        }

        usleep(10000); /* 10ms poll interval */
    }

    BridgeServer_Close();
    return 0;
}
