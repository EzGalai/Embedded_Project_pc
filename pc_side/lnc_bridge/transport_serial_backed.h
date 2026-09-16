/**
 * @file transport_serial_backed.h
 * @brief The serial-port-backed Transport_* implementation lnc_bridge links
 * against (see common/transport.h for the shared interface). This header
 * exists only for TransportSerialBacked_SetDevicePath — Transport_Init
 * itself is declared in transport.h, shared with the LNC firmware side,
 * which has no notion of a configurable device path.
 */
#ifndef TRANSPORT_SERIAL_BACKED_H
#define TRANSPORT_SERIAL_BACKED_H

/**
 * @brief Sets the serial device path Transport_Init will open (default
 * "/dev/lnc_board" — see the --lnc-port CLI flag). Call before Transport_Init.
 * @param path Device path (e.g. "/dev/ttyACM0").
 */
void TransportSerialBacked_SetDevicePath(const char *path);

#endif
