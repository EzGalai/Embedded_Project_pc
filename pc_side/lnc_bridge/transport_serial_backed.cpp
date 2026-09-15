#include "transport.h"
#include "transport_serial.h"
#include <cstdio>

void Transport_Init(void){
    /* /dev/lnc_board is a stable udev symlink (see /etc/udev/rules.d/99-nucleo.rules,
       matching the ST-LINK's USB idVendor:idProduct 0483:374b) — always points at
       whichever /dev/ttyACM* the kernel assigns on a given plug-in, so this path
       never needs updating after a USB re-enumeration. */
    if (!Serial_Open("/dev/lnc_board", 115200)) {
        fprintf(stderr, "lnc_bridge: failed to open serial port — check `ls -l /dev/lnc_board` exists and points at the current device\n");
    }
}

void Transport_Send(const uint8_t *data, uint16_t len){
    Serial_Send(data, len);
}


uint16_t Transport_Recv(uint8_t *outBuf, uint16_t maxLen){
    return Serial_Recv(outBuf,maxLen);
}
