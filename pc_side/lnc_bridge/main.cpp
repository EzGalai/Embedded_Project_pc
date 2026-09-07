#include "transport.h"
#include "protocol.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>

int main()
{
    Transport_Init();

    const uint8_t payload[] = "PHASE4FRAME";
    uint16_t payloadLen = (uint16_t)(sizeof(payload) - 1);

    uint8_t framed[64];
    uint16_t framedLen;
    if (Frame_Encode(payload, payloadLen, framed, sizeof(framed), &framedLen) != PROTO_OK) {
        std::printf("Frame_Encode failed\n");
        return 1;
    }

    Transport_Send(framed, framedLen);
    std::printf("Sent framed payload: %s\n", payload);

    usleep(200000);

    uint8_t rxBuf[128] = {0};
    size_t total = 0;
    for (int i = 0; i < 10; ++i) {
        total += Transport_Recv(rxBuf + total, (uint16_t)(sizeof(rxBuf) - total));
        usleep(50000);
    }
    std::printf("Received %zu raw bytes\n", total);

    uint8_t decoded[64];
    uint16_t decodedLen, consumed;
    ProtoResult_t r = Frame_Decode(rxBuf, (uint16_t)total, decoded, sizeof(decoded), &decodedLen, &consumed);

    bool match = false;
    if (r == PROTO_OK) {
        decoded[decodedLen] = '\0';
        std::printf("Decoded payload: %s\n", decoded);
        match = (decodedLen == payloadLen) && (std::memcmp(decoded, payload, payloadLen) == 0);
    } else {
        std::printf("Frame_Decode failed with result %d\n", (int)r);
    }

    std::printf("Phase 4 test: %s\n", match ? "PASSED" : "FAILED");
    return match ? 0 : 1;
}
