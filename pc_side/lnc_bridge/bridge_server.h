/**
 * @file bridge_server.h
 * @brief TCP server side of the lnc_bridge <-> central_computer link (Phase 6, see PROJECT_PLAN.md §4.11-4.13).
 *
 * lnc_bridge owns this TCP server; central_computer (or, for Phase 6's test, stub_core_client)
 * connects as the single client. Payloads crossing this link are core-side, uninterpreted byte
 * blobs — already unwrapped from LNC framing on the way up, and not yet re-wrapped on the way down.
 * They are protected only by TCP's own reliability plus a 4-byte big-endian length prefix per
 * message (no CRC/byte-stuffing needed here — unlike the UART link in protocol.h, this transport
 * is already loss-free and byte-stuffing would be redundant).
 *
 * lnc_bridge and central_computer run as two processes on the same PC (split apart for process
 * isolation, not for running on separate machines), so this server binds to loopback only
 * (127.0.0.1) internally — never reachable from another machine. Contrast with the future
 * central_computer <-> Ground Station link (Phase 7), which is a real network link between
 * different machines and will need a configurable bind address.
 *
 * The server is single-client and non-blocking: BridgeServer_Accept and BridgeServer_TryRecv never
 * block, so lnc_bridge's main loop can poll both the UART transport and this TCP link in the same
 * loop without either one stalling the other.
 */

#ifndef BRIDGE_SERVER_H
#define BRIDGE_SERVER_H

#include <cstdint>

/**
 * @brief Create, bind (to loopback), and listen on the server's TCP socket.
 *
 * Puts the listening socket into non-blocking mode. Must be called once before any other
 * BridgeServer_* function.
 *
 * @param port TCP port to listen on (host byte order).
 * @return true on success, false if the socket could not be created/bound/listened on.
 */
bool BridgeServer_Init(uint16_t port);

/**
 * @brief Non-blocking check for an incoming client connection.
 *
 * If no client is currently connected and one is waiting to connect, accepts it and puts the
 * new connection into non-blocking mode. Only one client is supported at a time — while a client
 * is already connected, this does nothing (a second connection attempt is left pending in the
 * listen backlog, not accepted). Safe to call every loop iteration.
 *
 * @return true if a client is connected after this call (whether newly accepted or already was), false otherwise.
 */
bool BridgeServer_Accept(void);

/**
 * @brief Send one payload to the connected client, length-prefixed.
 *
 * Writes a 4-byte big-endian length prefix followed by @p len bytes of @p data. Does nothing if
 * no client is currently connected. If the write fails (client disconnected), marks the
 * connection closed so the next BridgeServer_Accept can accept a new one.
 *
 * @param data Pointer to the payload bytes to send.
 * @param len Number of payload bytes (not counting the length prefix).
 */
void BridgeServer_Send(const uint8_t *data, uint16_t len);

/**
 * @brief Non-blocking attempt to receive one complete length-prefixed payload from the client.
 *
 * Reads whatever bytes are currently available and accumulates them internally across calls,
 * since a length prefix or payload may arrive split across multiple TCP segments. Returns a
 * complete payload only once its full length-prefixed frame has arrived.
 *
 * @param outBuf Buffer to receive the payload bytes into.
 * @param maxLen Capacity of @p outBuf, in bytes.
 * @return Number of payload bytes written to @p outBuf (0 if no complete payload is available yet,
 *         or if no client is connected).
 */
uint16_t BridgeServer_TryRecv(uint8_t *outBuf, uint16_t maxLen);

/**
 * @brief Whether a client is currently connected.
 * @return true if connected.
 */
bool BridgeServer_IsConnected(void);

/**
 * @brief Close the client connection (if any) and the listening socket.
 */
void BridgeServer_Close(void);

#endif // BRIDGE_SERVER_H
