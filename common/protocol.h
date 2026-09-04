/*
 * protocol.h — Submarine Monitoring System wire protocol
 *
 * Shared, byte-for-byte contract between the LNC firmware (C, STM32/FreeRTOS)
 * and the PC-side code (C++: lnc_bridge, central_computer, ground_station,
 * oop_fleet). Both sides #include this exact file — it is never copied or
 * reimplemented independently on either side.
 *
 * Source of truth: PROJECT_PLAN.md §3 (Protocol — TLV over a Framed Link).
 * This file currently covers §3.3–3.6 (TLV tag/field/status constants only).
 * Frame envelope constants (§3.2) and encode/decode function prototypes are
 * added in a later pass.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* ------------------------------------------------------------------------
 * Top-Level Message Tags (LNC ↔ Central Computer)
 * ------------------------------------------------------------------------ */
#define PROTO_TAG_SET_TEMP_NORMAL_RANGE     0x01u
#define PROTO_TAG_SET_TEMP_WARNING_RANGE    0x02u
#define PROTO_TAG_SET_HUMIDITY_NORMAL_MIN   0x03u
#define PROTO_TAG_SET_HUMIDITY_WARNING_MIN  0x04u
#define PROTO_TAG_SET_LIGHT_NORMAL_MIN      0x05u
#define PROTO_TAG_SET_LIGHT_WARNING_MIN     0x06u
#define PROTO_TAG_SET_BATTERY_NORMAL_MIN    0x07u
#define PROTO_TAG_SET_BATTERY_WARNING_MIN   0x08u
#define PROTO_TAG_SET_RTC_REQ               0x09u
#define PROTO_TAG_GET_TIME_REQ              0x0Au
#define PROTO_TAG_GET_TIME_RESP             0x0Bu
#define PROTO_TAG_CONFIG_ACK                0x0Cu
#define PROTO_TAG_GET_MEASUREMENTS_REQ      0x0Du
#define PROTO_TAG_GET_MEASUREMENTS_RESP     0x0Eu
#define PROTO_TAG_GET_EVENTS_REQ            0x0Fu
#define PROTO_TAG_GET_EVENTS_RESP           0x10u
#define PROTO_TAG_KEEP_ALIVE                0x11u
#define PROTO_TAG_EVENT_REPORT              0x12u

/* ------------------------------------------------------------------------
 * Ground Station ↔ Central Computer Tags
 * ------------------------------------------------------------------------ */
#define PROTO_TAG_GS_GET_LOG_REQ            0x13u
#define PROTO_TAG_GS_GET_LOG_RESP           0x14u
#define PROTO_TAG_GS_GET_EVENTS_REQ         0x15u
#define PROTO_TAG_GS_GET_EVENTS_RESP        0x16u

/* ------------------------------------------------------------------------
 * Field Tags (nested inside message values)
 * ------------------------------------------------------------------------ */
#define PROTO_FIELD_TIMESTAMP               0x20u
#define PROTO_FIELD_TEMP_LOW                0x21u
#define PROTO_FIELD_TEMP_HIGH               0x22u
#define PROTO_FIELD_HUMIDITY_MIN            0x23u
#define PROTO_FIELD_LIGHT_MIN               0x24u
#define PROTO_FIELD_BATTERY_MIN             0x25u
#define PROTO_FIELD_STATUS                  0x26u
#define PROTO_FIELD_TIME_RANGE_START        0x27u
#define PROTO_FIELD_TIME_RANGE_END          0x28u
#define PROTO_FIELD_MEASUREMENT_RECORD      0x29u
#define PROTO_FIELD_EVENT_TYPE              0x2Au
#define PROTO_FIELD_EVENT_SOURCE            0x2Bu
#define PROTO_FIELD_MODE                    0x2Cu
#define PROTO_FIELD_WD_RESET_FLAG           0x2Du
#define PROTO_FIELD_TEMPERATURE             0x2Eu
#define PROTO_FIELD_HUMIDITY                0x2Fu
#define PROTO_FIELD_LIGHT                   0x30u
#define PROTO_FIELD_BATTERY_VOLTAGE         0x31u
#define PROTO_FIELD_SUBMARINE_ID            0x32u

/* ------------------------------------------------------------------------
 * 3.6 — Status codes (value of PROTO_FIELD_STATUS)
 * ------------------------------------------------------------------------
 * Plain C enum (not C++ `enum class`, for compatibility with the LNC's C
 * build) so status-handling code gets switch-exhaustiveness checking
 * (-Wswitch) and readable values in a debugger, instead of a bare uint8_t.
 * The wire is still always written/read as a single explicit byte in the
 * codec — this typedef only affects how the value is held in memory.
 */
typedef enum {
    PROTO_STATUS_SUCCESS            = 0x00,
    PROTO_STATUS_INVALID_RANGE      = 0x01,  /* e.g. low > high */
    PROTO_STATUS_INVALID_TIME_RANGE = 0x02,
    PROTO_STATUS_NO_DATA_FOUND      = 0x03,
    PROTO_STATUS_RTC_NOT_SYNCED     = 0x04,
    PROTO_STATUS_INTERNAL_ERROR     = 0x05
} ProtoStatus_t;

/* ------------------------------------------------------------------------
 * Enumerated sub-values carried inside specific fields
 * ------------------------------------------------------------------------
 * Same reasoning as ProtoStatus_t above.
 */

/* Value of PROTO_FIELD_MODE */
typedef enum {
    PROTO_MODE_NORMAL  = 0x00,
    PROTO_MODE_WARNING = 0x01,
    PROTO_MODE_ERROR   = 0x02
} ProtoMode_t;

/* Value of PROTO_FIELD_EVENT_TYPE */
typedef enum {
    PROTO_EVENT_TYPE_MODE_CHANGE     = 0x00,
    PROTO_EVENT_TYPE_OBJECT_DETECTED = 0x01,
    PROTO_EVENT_TYPE_OBJECT_CLEARED  = 0x02,
    PROTO_EVENT_TYPE_CONFIG_CHANGED  = 0x03,
    PROTO_EVENT_TYPE_STARTUP         = 0x04
} ProtoEventType_t;

/* Value of PROTO_FIELD_EVENT_SOURCE */
typedef enum {
    PROTO_EVENT_SOURCE_MONITOR          = 0x00,
    PROTO_EVENT_SOURCE_OBJECT_DETECTION = 0x01,
    PROTO_EVENT_SOURCE_CONFIGURATION    = 0x02,
    PROTO_EVENT_SOURCE_INIT             = 0x03
} ProtoEventSource_t;

/* ------------------------------------------------------------------------
 * Codec result codes
 * ------------------------------------------------------------------------
 * Local "did this C call succeed" signaling for the functions below.
 * Distinct from ProtoStatus_t on purpose: ProtoStatus_t is an application-
 * level wire value that travels inside a CONFIG_ACK/*_RESP message; this
 * enum never crosses the wire, it only reports on the codec call itself.
 */
typedef enum {
    PROTO_OK = 0,
    PROTO_ERR_BUFFER_TOO_SMALL,  /* not enough room in the output buffer */
    PROTO_ERR_INCOMPLETE,        /* not enough input bytes yet — wait for more */
    PROTO_ERR_MALFORMED          /* input is structurally invalid (reserved for the frame layer) */
} ProtoResult_t;

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------
 * Endianness-safe primitives
 * ------------------------------------------------------------------------
 * Fixed big-endian ("network order") regardless of host CPU endianness —
 * see PROJECT_PLAN.md's byte-order discussion. Neither Put nor Get performs
 * bounds checking; the caller is responsible for ensuring the buffer has
 * enough room (2 or 4 bytes respectively). Each returns a pointer just past
 * the bytes it wrote/read, so calls can be chained.
 */
/**
 * @brief Write a 16-bit value into a buffer in fixed big-endian order.
 * @param buf   Destination buffer; must have at least 2 bytes of room (not checked).
 * @param value Value to write.
 * @return Pointer to buf + 2 (just past the bytes written), for chaining.
 */
uint8_t *Protocol_PutU16(uint8_t *buf, uint16_t value);

/**
 * @brief Write a 32-bit value into a buffer in fixed big-endian order.
 * @param buf   Destination buffer; must have at least 4 bytes of room (not checked).
 * @param value Value to write.
 * @return Pointer to buf + 4 (just past the bytes written), for chaining.
 */
uint8_t *Protocol_PutU32(uint8_t *buf, uint32_t value);

/**
 * @brief Read a 16-bit value from a buffer in fixed big-endian order.
 * @param buf      Source buffer; must have at least 2 bytes available (not checked).
 * @param outValue Set to the decoded value.
 * @return Pointer to buf + 2 (just past the bytes read), for chaining.
 */
const uint8_t *Protocol_GetU16(const uint8_t *buf, uint16_t *outValue);

/**
 * @brief Read a 32-bit value from a buffer in fixed big-endian order.
 * @param buf      Source buffer; must have at least 4 bytes available (not checked).
 * @param outValue Set to the decoded value.
 * @return Pointer to buf + 4 (just past the bytes read), for chaining.
 */
const uint8_t *Protocol_GetU32(const uint8_t *buf, uint32_t *outValue);

/* ------------------------------------------------------------------------
 * TLV encode/decode (§3.1)
 * ------------------------------------------------------------------------
 */

/**
 * @brief Encode one TLV: [tag(1)][length(2, big-endian)][value(valueLen)].
 * @param tag        Tag byte to write.
 * @param value      Value bytes to write (may be NULL if valueLen is 0).
 * @param valueLen   Length of value, in bytes.
 * @param outBuf     Destination buffer.
 * @param outBufCap  Capacity of outBuf, in bytes.
 * @param outWritten Set to the total bytes written (3 + valueLen) on success.
 * @return PROTO_OK on success, PROTO_ERR_BUFFER_TOO_SMALL if outBufCap < 3 + valueLen.
 */
ProtoResult_t Protocol_EncodeTLV(uint8_t tag, const uint8_t *value, uint16_t valueLen,
                                  uint8_t *outBuf, uint16_t outBufCap, uint16_t *outWritten);

/**
 * @brief Decode one TLV starting at inBuf.
 *
 * Never returns PROTO_ERR_MALFORMED: any tag/length combination is
 * structurally valid at this layer — whether the tag is a *known* tag is a
 * dispatch-level concern for the caller, not a parsing error.
 *
 * @param inBuf       Input buffer to parse.
 * @param inLen       Bytes available in inBuf.
 * @param outTag      Set to the decoded tag byte on success.
 * @param outValue    Set to point into inBuf at the value bytes (no copy) on success.
 * @param outValueLen Set to the decoded value length on success.
 * @param outConsumed Set to the total bytes consumed (3 + *outValueLen) on success,
 *                     so the caller can advance past this TLV to parse the next one.
 * @return PROTO_OK on success, or PROTO_ERR_INCOMPLETE if inLen is too short to
 *         contain the tag+length, or too short for the value the length field
 *         declares — the caller should wait for more bytes and retry, per §3.7's
 *         byte-stream parsing strategy.
 */
ProtoResult_t Protocol_DecodeTLV(const uint8_t *inBuf, uint16_t inLen,
                                  uint8_t *outTag, const uint8_t **outValue,
                                  uint16_t *outValueLen, uint16_t *outConsumed);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */
