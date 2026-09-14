#include "transport.h"
#include "transport_serial.h"
#include <cstdio>

void Transport_Init(void){
    if (!Serial_Open("/dev/ttyACM0", 115200)) {
        fprintf(stderr, "lnc_bridge: failed to open serial port — check `ls /dev/ttyACM*` for the current device path\n");
    }
}

void Transport_Send(const uint8_t *data, uint16_t len){
    Serial_Send(data, len);
}


uint16_t Transport_Recv(uint8_t *outBuf, uint16_t maxLen){
    return Serial_Recv(outBuf,maxLen);
}
