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
