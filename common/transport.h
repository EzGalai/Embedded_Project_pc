/*
 * transport.h — transport-independent interface (PROJECT_PLAN.md §4.10)
 *
 * Shared contract between the LNC firmware and the PC-side code. Whatever
 * concrete backend a build links against (UART on the LNC, a serial port
 * in lnc_bridge, or this mock for host-side testing), calling code only
 * ever uses these three functions — never anything backend-specific.
 */

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the transport (sets up whatever concrete backend this
 * build is linked against).
 */
void Transport_Init(void);

/**
 * @brief Send bytes over the transport, blocking until fully sent.
 * @param data Bytes to send.
 * @param len  Number of bytes to send.
 */
void Transport_Send(const uint8_t *data, uint16_t len);

/**
 * @brief Read whatever bytes are currently available, without blocking.
 * @param outBuf Destination buffer.
 * @param maxLen Maximum bytes outBuf can hold.
 * @return Number of bytes actually read; 0 if nothing is available yet.
 */
uint16_t Transport_Recv(uint8_t *outBuf, uint16_t maxLen);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_H */
