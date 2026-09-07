#include "transport_serial.h"
#include "protocol.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>

int main()
{
    if (!Serial_Open("/dev/ttyACM0", 115200)) {
        std::printf("Failed to open serial port\n");
        return 1;
    }

    const uint8_t payload[] = "PHASE3FRAME";
    uint16_t payloadLen = (uint16_t)(sizeof(payload) - 1);

    uint8_t framed[64];
    uint16_t framedLen;
    if (Frame_Encode(payload, payloadLen, framed, sizeof(framed), &framedLen) != PROTO_OK) {
        std::printf("Frame_Encode failed\n");
        Serial_Close();
        return 1;
    }

    if (!Serial_Send(framed, framedLen)) {
        std::printf("Send failed\n");
        Serial_Close();
        return 1;
    }
    std::printf("Sent framed payload: %s\n", payload);

    usleep(200000);

    uint8_t rxBuf[128] = {0};
    size_t total = 0;
    for (int i = 0; i < 10; ++i) {
        total += Serial_Recv(rxBuf + total, sizeof(rxBuf) - total);
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

    std::printf("Phase 3 test: %s\n", match ? "PASSED" : "FAILED");

    Serial_Close();
    return match ? 0 : 1;
}