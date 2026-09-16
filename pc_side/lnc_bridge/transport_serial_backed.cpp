#include "transport.h"
#include "transport_serial_backed.h"
#include "transport_serial.h"
#include <cstdio>

/* Default is the stable udev symlink (see /etc/udev/rules.d/99-nucleo.rules,
   matching the ST-LINK's USB idVendor:idProduct 0483:374b) — always points at
   whichever /dev/ttyACM* the kernel assigns on a given plug-in, so this path
   never needs updating after a USB re-enumeration. Overridable via --lnc-port. */
static const char *g_devicePath = "/dev/lnc_board";

void TransportSerialBacked_SetDevicePath(const char *path)
{
    g_devicePath = path;
}

void Transport_Init(void){
    if (!Serial_Open(g_devicePath, 115200)) {
        fprintf(stderr, "lnc_bridge: failed to open serial port '%s' — check `ls -l %s` exists and points at the current device\n",
                g_devicePath, g_devicePath);
    }
}

void Transport_Send(const uint8_t *data, uint16_t len){
    Serial_Send(data, len);
}


uint16_t Transport_Recv(uint8_t *outBuf, uint16_t maxLen){
    return Serial_Recv(outBuf,maxLen);
}
