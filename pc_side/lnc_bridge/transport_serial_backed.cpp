#include "transport.h"
#include "transport_serial.h"


void Transport_Init(void){
    Serial_Open("/dev/ttyACM0", 115200);
}

void Transport_Send(const uint8_t *data, uint16_t len){
    Serial_Send(data, len);
}


uint16_t Transport_Recv(uint8_t *outBuf, uint16_t maxLen){
    return Serial_Recv(outBuf,maxLen);
}
