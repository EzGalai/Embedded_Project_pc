/**
 * @file lnc_link_client.h
 * @brief TCP client to lnc_bridge (PROJECT_PLAN.md §4.13/§7). Owns the raw
 * connection and the length-prefixed TLV send/receive primitives that
 * every other LNC-facing module (management_command.cpp, log.cpp) builds
 * on.
 */
#ifndef LNC_LINK_CLIENT_H
#define LNC_LINK_CLIENT_H

#include <cstdint>
#include <cstddef>
#include <string>

#define BRIDGE_TCP_PORT 5100

/**
 * @brief Formats a Unix timestamp (seconds since 1970-01-01 UTC) as a
 * human-readable "YYYY-MM-DD HH:MM:SS" string, for display only — the wire
 * protocol keeps the raw value, matching how light/battery are converted to
 * percentages for display but kept raw on the wire.
 * @param timestamp Seconds since the Unix epoch.
 * @param outBuf Destination buffer.
 * @param bufSize Capacity of outBuf.
 */
void FormatUnixTime(uint32_t timestamp, char *outBuf, size_t bufSize);

/**
 * @brief Connects to an lnc_bridge's TCP bridge link.
 * @param host Address to connect to (defaults to the standalone
 * central_computer process's usual loopback lnc_bridge).
 * @param port Port to connect to.
 * @return Connected socket fd, or -1 on failure.
 */
int CcCore_LncConnect(const char *host = "127.0.0.1", uint16_t port = BRIDGE_TCP_PORT);

/**
 * @brief Reads one length-prefixed TLV payload from lnc_bridge into outBuf.
 * @return Payload length, or 0 on disconnect/error/oversized message.
 */
uint16_t CcCore_LncRecv(int fd, uint8_t *outBuf, uint16_t maxLen);

/**
 * @brief TLV-encode a message and send it length-prefixed to lnc_bridge.
 * @param fd Connected socket.
 * @param tag Message tag.
 * @param value Value bytes (may be nullptr if valueLen is 0).
 * @param valueLen Length of value.
 */
void CcCore_LncSend(int fd, uint8_t tag, const uint8_t *value, uint16_t valueLen);

/**
 * @brief Reads and decodes one message from lnc_bridge, transparently
 * printing (and skipping) any KEEP_ALIVE or EVENT_REPORT that arrives
 * along the way (see log.cpp).
 * @param fd Connected socket.
 * @param outTag Set to the decoded tag of the first non-KEEP_ALIVE/EVENT_REPORT message.
 * @param outValue Set to point at that message's Value.
 * @param outValueLen Set to that Value's length.
 * @param storage Caller-provided buffer outValue points into (must outlive outValue's use).
 * @param storageCap Capacity of storage.
 * @param submarineId Whose telemetry this is, for DCA storage — defaults to the
 * standalone central_computer process's single submarine.
 * @return true on success, false on disconnect or malformed data.
 */
bool CcCore_LncRecvMessage(int fd, uint8_t *outTag, const uint8_t **outValue, uint16_t *outValueLen,
                           uint8_t *storage, uint16_t storageCap, const std::string &submarineId = "LNC-01");

#endif
