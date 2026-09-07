#ifndef TRANSPORT_SERIAL_H
#define TRANSPORT_SERIAL_H

#include <cstdint>
#include <cstddef>

/**
 * @brief Open and configure the serial port to the LNC.
 * @param device   Path to the serial device (e.g. "/dev/ttyACM0").
 * @param baudRate Baud rate to configure (e.g. 115200).
 * @return true on success, false on failure (device not found, permission denied, etc.)
 */
bool Serial_Open(const char *device, int baudRate);

/**
 * @brief Send bytes over the open serial port, blocking until fully written.
 * @param data Bytes to send.
 * @param len  Number of bytes to send.
 * @return true on success, false on a write error.
 */
bool Serial_Send(const uint8_t *data, size_t len);

/**
 * @brief Read whatever bytes are currently available, without blocking.
 * @param outBuf Destination buffer.
 * @param maxLen Maximum bytes outBuf can hold.
 * @return Number of bytes actually read; 0 if nothing is available.
 */
size_t Serial_Recv(uint8_t *outBuf, size_t maxLen);

/**
 * @brief Close the serial port.
 */
void Serial_Close(void);

#endif /* TRANSPORT_SERIAL_H */
