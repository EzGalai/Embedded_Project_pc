#include "protocol.h"
#include <string.h>

uint8_t *Protocol_PutU16(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value >> 8);
    buf[1] = (uint8_t)(value & 0xFFu);
    return buf + 2;
}

uint8_t *Protocol_PutU32(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value >> 24);
    buf[1] = (uint8_t)(value >> 16);
    buf[2] = (uint8_t)(value >> 8);
    buf[3] = (uint8_t)(value & 0xFFu);
    return buf + 4;
}

const uint8_t *Protocol_GetU16(const uint8_t *buf, uint16_t *outValue)
{
    *outValue = (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
    return buf + 2;
}

const uint8_t *Protocol_GetU32(const uint8_t *buf, uint32_t *outValue)
{
    *outValue = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                ((uint32_t)buf[2] << 8)  | (uint32_t)buf[3];
    return buf + 4;
}

ProtoResult_t Protocol_EncodeTLV(uint8_t tag, const uint8_t *value, uint16_t valueLen,
                                  uint8_t *outBuf, uint16_t outBufCap, uint16_t *outWritten)
{
    uint16_t total = (uint16_t)(3u + valueLen); /* tag(1) + length(2) + value */

    if (outBufCap < total) {
        return PROTO_ERR_BUFFER_TOO_SMALL;
    }

    outBuf[0] = tag;
    Protocol_PutU16(outBuf + 1, valueLen);

    if (valueLen > 0u) { /* memcpy with a NULL src is UB even when len is 0 in practice, so guard it */
        memcpy(outBuf + 3, value, valueLen);
    }

    *outWritten = total;
    return PROTO_OK;
}

ProtoResult_t Protocol_DecodeTLV(const uint8_t *inBuf, uint16_t inLen,
                                  uint8_t *outTag, const uint8_t **outValue,
                                  uint16_t *outValueLen, uint16_t *outConsumed)
{
    uint16_t valueLen;

    if (inLen < 3u) {
        return PROTO_ERR_INCOMPLETE; /* tag+length themselves haven't fully arrived yet */
    }

    Protocol_GetU16(inBuf + 1, &valueLen);

    if (inLen < (uint16_t)(3u + valueLen)) {
        return PROTO_ERR_INCOMPLETE; /* header is here, but value bytes are still in flight */
    }

    *outTag = inBuf[0];
    *outValue = inBuf + 3; /* points into inBuf — no copy */
    *outValueLen = valueLen;
    *outConsumed = (uint16_t)(3u + valueLen);
    return PROTO_OK;
}

ProtoResult_t Protocol_FindField(const uint8_t *buf, uint16_t len, uint8_t tag,
                                  const uint8_t **outValue, uint16_t *outValueLen)
{
    uint16_t offset = 0u;

    while (offset < len) {
        uint8_t curTag;
        const uint8_t *curValue;
        uint16_t curValueLen;
        uint16_t consumed;
        ProtoResult_t r = Protocol_DecodeTLV(buf + offset, (uint16_t)(len - offset),
                                              &curTag, &curValue, &curValueLen, &consumed);

        if (r != PROTO_OK) {
            /* the whole buffer is already in hand, so "incomplete" here really means
             * a field's declared length runs past the end of buf — i.e. malformed */
            return PROTO_ERR_MALFORMED;
        }

        if (curTag == tag) {
            *outValue = curValue;
            *outValueLen = curValueLen;
            return PROTO_OK;
        }

        offset = (uint16_t)(offset + consumed);
    }

    return PROTO_ERR_NOT_FOUND;
}

/* Shared by Protocol_Crc16 and the frame functions, which need to checksum
 * LENGTH and the payload as two separate calls rather than one buffer. */
static uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    int bit;

    for (i = 0; i < len; i++) {
        crc = (uint16_t)(crc ^ ((uint16_t)data[i] << 8));
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t Protocol_Crc16(const uint8_t *data, uint16_t len)
{
    return crc16_update(0xFFFFu, data, len);
}

/* Writes one byte into out[*outIdx], escaping it first if it collides with
 * START/ESCAPE. Returns 1 on success, 0 if outCap would be exceeded. */
static int stuff_write_byte(uint8_t *out, uint16_t outCap, uint16_t *outIdx, uint8_t b)
{
    if (b == PROTO_FRAME_START || b == PROTO_FRAME_ESCAPE) {
        if ((uint16_t)(*outIdx + 2u) > outCap) {
            return 0;
        }
        out[(*outIdx)++] = PROTO_FRAME_ESCAPE;
        out[(*outIdx)++] = (uint8_t)(b ^ PROTO_FRAME_ESCAPE_XOR);
    } else {
        if ((uint16_t)(*outIdx + 1u) > outCap) {
            return 0;
        }
        out[(*outIdx)++] = b;
    }
    return 1;
}

/* Reads one de-stuffed byte from in[*inIdx], advancing *inIdx by 1 or 2.
 * Returns 1 on success, 0 if there aren't enough raw bytes left yet (e.g. a
 * trailing escape byte whose pair hasn't arrived). */
static int destuff_read_byte(const uint8_t *in, uint16_t inLen, uint16_t *inIdx, uint8_t *outByte)
{
    uint8_t b;

    if (*inIdx >= inLen) {
        return 0;
    }

    b = in[*inIdx];
    if (b == PROTO_FRAME_ESCAPE) {
        if ((uint16_t)(*inIdx + 1u) >= inLen) {
            return 0;
        }
        *outByte = (uint8_t)(in[*inIdx + 1u] ^ PROTO_FRAME_ESCAPE_XOR);
        *inIdx = (uint16_t)(*inIdx + 2u);
    } else {
        *outByte = b;
        *inIdx = (uint16_t)(*inIdx + 1u);
    }
    return 1;
}

ProtoResult_t Frame_Encode(const uint8_t *payload, uint16_t payloadLen,
                            uint8_t *outBuf, uint16_t outBufCap, uint16_t *outWritten)
{
    uint8_t lenBytes[2];
    uint8_t crcBytes[2];
    uint16_t crc;
    uint16_t idx = 0u;
    uint16_t i;

    Protocol_PutU16(lenBytes, payloadLen);
    crc = crc16_update(0xFFFFu, lenBytes, 2u);
    crc = crc16_update(crc, payload, payloadLen);
    Protocol_PutU16(crcBytes, crc);

    if (outBufCap < 1u) {
        return PROTO_ERR_BUFFER_TOO_SMALL;
    }
    outBuf[idx++] = PROTO_FRAME_START; /* the only byte allowed to be a literal, un-stuffed 0x7E */

    for (i = 0; i < 2u; i++) {
        if (!stuff_write_byte(outBuf, outBufCap, &idx, lenBytes[i])) return PROTO_ERR_BUFFER_TOO_SMALL;
    }
    for (i = 0; i < payloadLen; i++) {
        if (!stuff_write_byte(outBuf, outBufCap, &idx, payload[i])) return PROTO_ERR_BUFFER_TOO_SMALL;
    }
    for (i = 0; i < 2u; i++) {
        if (!stuff_write_byte(outBuf, outBufCap, &idx, crcBytes[i])) return PROTO_ERR_BUFFER_TOO_SMALL;
    }

    *outWritten = idx;
    return PROTO_OK;
}

ProtoResult_t Frame_Decode(const uint8_t *inBuf, uint16_t inLen,
                            uint8_t *outPayload, uint16_t outPayloadCap,
                            uint16_t *outPayloadLen, uint16_t *outConsumed)
{
    uint16_t startIdx = 0u;
    uint16_t idx;
    uint8_t lenBytes[2];
    uint8_t crcBytes[2];
    uint16_t payloadLen;
    uint16_t receivedCrc;
    uint16_t computedCrc;
    uint16_t i;

    while (startIdx < inLen && inBuf[startIdx] != PROTO_FRAME_START) {
        startIdx++;
    }
    if (startIdx >= inLen) {
        return PROTO_ERR_INCOMPLETE; /* no start byte in the buffer at all yet */
    }
    idx = (uint16_t)(startIdx + 1u);

    for (i = 0; i < 2u; i++) {
        if (!destuff_read_byte(inBuf, inLen, &idx, &lenBytes[i])) {
            return PROTO_ERR_INCOMPLETE;
        }
    }
    Protocol_GetU16(lenBytes, &payloadLen);

    if (payloadLen > outPayloadCap) {
        return PROTO_ERR_BUFFER_TOO_SMALL;
    }

    for (i = 0; i < payloadLen; i++) {
        if (!destuff_read_byte(inBuf, inLen, &idx, &outPayload[i])) {
            return PROTO_ERR_INCOMPLETE;
        }
    }

    for (i = 0; i < 2u; i++) {
        if (!destuff_read_byte(inBuf, inLen, &idx, &crcBytes[i])) {
            return PROTO_ERR_INCOMPLETE;
        }
    }
    Protocol_GetU16(crcBytes, &receivedCrc);

    computedCrc = crc16_update(0xFFFFu, lenBytes, 2u);
    computedCrc = crc16_update(computedCrc, outPayload, payloadLen);

    *outConsumed = idx; /* everything from the start of inBuf through this frame, incl. any leading noise */

    if (computedCrc != receivedCrc) {
        return PROTO_ERR_MALFORMED;
    }

    *outPayloadLen = payloadLen;
    return PROTO_OK;
}
