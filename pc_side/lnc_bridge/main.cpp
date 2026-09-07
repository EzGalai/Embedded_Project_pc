#include "transport_serial.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>

int main()
{
    if (!Serial_Open("/dev/ttyACM0", 115200)) {
        std::printf("Failed to open serial port\n");
        return 1;
    }

    const uint8_t msg[] = "PHASE2ECHO";
    if (!Serial_Send(msg, sizeof(msg) - 1)) {
        std::printf("Send failed\n");
        Serial_Close();
        return 1;
    }
    std::printf("Sent: %s\n", msg);

    usleep(200000); /* give the board a moment to echo back */

    uint8_t rxBuf[64] = {0};
    size_t total = 0;
    for (int i = 0; i < 10 && total < sizeof(msg) - 1; ++i) {
        total += Serial_Recv(rxBuf + total, sizeof(rxBuf) - total);
        if (total < sizeof(msg) - 1) usleep(50000);
    }

    rxBuf[total] = '\0';
    std::printf("Received: %s\n", rxBuf);

    bool match = (total == sizeof(msg) - 1) && (std::memcmp(msg, rxBuf, total) == 0);
    std::printf("Phase 2 test: %s\n", match ? "PASSED" : "FAILED");

    Serial_Close();
    return match ? 0 : 1;
}
