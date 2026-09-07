/*
 * test_protocol.c — host-side unit tests for protocol.c/.h (Phase 5 test).
 *
 * No hardware needed: this exercises the shared TLV codec in isolation,
 * exactly as PROJECT_PLAN.md's Phase 5 requires before Phase 6 (lnc_bridge)
 * starts. Not compiled into the firmware or any PC executable — it's a
 * standalone check on this one file pair.
 */

#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_u16_roundtrip(void)
{
    uint8_t buf[2];
    uint16_t decoded;

    Protocol_PutU16(buf, 0x0000u);
    Protocol_GetU16(buf, &decoded);
    assert(decoded == 0x0000u);

    Protocol_PutU16(buf, 0xFFFFu);
    Protocol_GetU16(buf, &decoded);
    assert(decoded == 0xFFFFu);

    Protocol_PutU16(buf, 0x1234u);
    assert(buf[0] == 0x12u && buf[1] == 0x34u); /* confirms big-endian byte order */
    Protocol_GetU16(buf, &decoded);
    assert(decoded == 0x1234u);

    printf("PASS: u16 put/get round-trip\n");
}

static void test_u32_roundtrip(void)
{
    uint8_t buf[4];
    uint32_t decoded;

    Protocol_PutU32(buf, 0x00000000u);
    Protocol_GetU32(buf, &decoded);
    assert(decoded == 0x00000000u);

    Protocol_PutU32(buf, 0xFFFFFFFFu);
    Protocol_GetU32(buf, &decoded);
    assert(decoded == 0xFFFFFFFFu);

    Protocol_PutU32(buf, 0x12345678u);
    assert(buf[0] == 0x12u && buf[1] == 0x34u && buf[2] == 0x56u && buf[3] == 0x78u);
    Protocol_GetU32(buf, &decoded);
    assert(decoded == 0x12345678u);

    printf("PASS: u32 put/get round-trip\n");
}

static void test_tlv_roundtrip_empty(void)
{
    uint8_t buf[8];
    uint16_t written;
    uint8_t tag;
    const uint8_t *value;
    uint16_t valueLen, consumed;
    ProtoResult_t r;

    r = Protocol_EncodeTLV(PROTO_TAG_GET_TIME_REQ, NULL, 0u, buf, sizeof(buf), &written);
    assert(r == PROTO_OK);
    assert(written == 3u); /* tag(1) + length(2) + value(0) */

    r = Protocol_DecodeTLV(buf, written, &tag, &value, &valueLen, &consumed);
    assert(r == PROTO_OK);
    assert(tag == PROTO_TAG_GET_TIME_REQ);
    assert(valueLen == 0u);
    assert(consumed == 3u);

    printf("PASS: TLV round-trip, empty value\n");
}

static void test_tlv_roundtrip_small_value(void)
{
    uint8_t fieldValue[2];
    uint8_t buf[8];
    uint16_t written;
    uint8_t tag;
    const uint8_t *value;
    uint16_t valueLen, consumed, decodedTemp;
    ProtoResult_t r;

    Protocol_PutU16(fieldValue, 300u); /* 30.0 degC in tenths */

    r = Protocol_EncodeTLV(PROTO_FIELD_TEMP_HIGH, fieldValue, sizeof(fieldValue),
                            buf, sizeof(buf), &written);
    assert(r == PROTO_OK);
    assert(written == 5u); /* tag(1) + length(2) + value(2) */

    r = Protocol_DecodeTLV(buf, written, &tag, &value, &valueLen, &consumed);
    assert(r == PROTO_OK);
    assert(tag == PROTO_FIELD_TEMP_HIGH);
    assert(valueLen == 2u);
    assert(consumed == 5u);

    Protocol_GetU16(value, &decodedTemp);
    assert(decodedTemp == 300u);

    printf("PASS: TLV round-trip, 2-byte value\n");
}

static void test_decode_incomplete(void)
{
    /* Declares a 2-byte value but only 1 byte of it is actually present. */
    uint8_t buf[4] = { PROTO_FIELD_TEMP_HIGH, 0x00u, 0x02u, 0xAAu };
    uint8_t tag;
    const uint8_t *value;
    uint16_t valueLen, consumed;
    ProtoResult_t r;

    r = Protocol_DecodeTLV(buf, sizeof(buf), &tag, &value, &valueLen, &consumed);
    assert(r == PROTO_ERR_INCOMPLETE);

    /* Not even the tag+length header is fully present. */
    r = Protocol_DecodeTLV(buf, 2u, &tag, &value, &valueLen, &consumed);
    assert(r == PROTO_ERR_INCOMPLETE);

    printf("PASS: DecodeTLV incomplete-buffer detection\n");
}

static void test_encode_buffer_too_small(void)
{
    uint8_t fieldValue[2] = { 0x01u, 0x2Cu };
    uint8_t tinyBuf[3]; /* needs 5 bytes total, only 3 available */
    uint16_t written;
    ProtoResult_t r;

    r = Protocol_EncodeTLV(PROTO_FIELD_TEMP_HIGH, fieldValue, sizeof(fieldValue),
                            tinyBuf, sizeof(tinyBuf), &written);
    assert(r == PROTO_ERR_BUFFER_TOO_SMALL);

    printf("PASS: EncodeTLV buffer-too-small detection\n");
}

static void test_find_field(void)
{
    /* TEMP_LOW + TEMP_HIGH as sibling field TLVs, like SET_TEMP_WARNING_RANGE's value. */
    uint8_t buf[32];
    uint16_t offset = 0u, written;
    uint8_t lowVal[2], highVal[2];
    const uint8_t *found;
    uint16_t foundLen, decoded;
    ProtoResult_t r;

    Protocol_PutU16(lowVal, 100u);
    Protocol_PutU16(highVal, 300u);

    Protocol_EncodeTLV(PROTO_FIELD_TEMP_LOW, lowVal, 2u, buf + offset, (uint16_t)(sizeof(buf) - offset), &written);
    offset = (uint16_t)(offset + written);
    Protocol_EncodeTLV(PROTO_FIELD_TEMP_HIGH, highVal, 2u, buf + offset, (uint16_t)(sizeof(buf) - offset), &written);
    offset = (uint16_t)(offset + written);

    /* Find TEMP_HIGH even though it's second in the buffer — order-independence. */
    r = Protocol_FindField(buf, offset, PROTO_FIELD_TEMP_HIGH, &found, &foundLen);
    assert(r == PROTO_OK);
    assert(foundLen == 2u);
    Protocol_GetU16(found, &decoded);
    assert(decoded == 300u);

    r = Protocol_FindField(buf, offset, PROTO_FIELD_TEMP_LOW, &found, &foundLen);
    assert(r == PROTO_OK);
    Protocol_GetU16(found, &decoded);
    assert(decoded == 100u);

    /* A tag that isn't present at all. */
    r = Protocol_FindField(buf, offset, PROTO_FIELD_HUMIDITY_MIN, &found, &foundLen);
    assert(r == PROTO_ERR_NOT_FOUND);

    printf("PASS: FindField sibling lookup + not-found\n");
}

static void test_find_field_nested(void)
{
    /* A small MEASUREMENT_RECORD-style value (TEMPERATURE + MODE only, enough to
     * prove the nested/two-call lookup pattern without every real field). */
    uint8_t innerBuf[16];
    uint16_t innerOffset = 0u, written;
    uint8_t tempVal[2];
    uint8_t modeVal[1];
    uint8_t outerBuf[32];
    uint16_t outerWritten;
    const uint8_t *recordValue, *tempField;
    uint16_t recordLen, tempFieldLen, decodedTemp;
    ProtoResult_t r;

    modeVal[0] = (uint8_t)PROTO_MODE_WARNING;
    Protocol_PutU16(tempVal, 305u);

    Protocol_EncodeTLV(PROTO_FIELD_TEMPERATURE, tempVal, 2u,
                        innerBuf + innerOffset, (uint16_t)(sizeof(innerBuf) - innerOffset), &written);
    innerOffset = (uint16_t)(innerOffset + written);
    Protocol_EncodeTLV(PROTO_FIELD_MODE, modeVal, 1u,
                        innerBuf + innerOffset, (uint16_t)(sizeof(innerBuf) - innerOffset), &written);
    innerOffset = (uint16_t)(innerOffset + written);

    /* Wrap that as one MEASUREMENT_RECORD field TLV inside an outer buffer. */
    Protocol_EncodeTLV(PROTO_FIELD_MEASUREMENT_RECORD, innerBuf, innerOffset,
                        outerBuf, sizeof(outerBuf), &outerWritten);

    /* First call: find MEASUREMENT_RECORD itself. */
    r = Protocol_FindField(outerBuf, outerWritten, PROTO_FIELD_MEASUREMENT_RECORD,
                            &recordValue, &recordLen);
    assert(r == PROTO_OK);
    assert(recordLen == innerOffset);

    /* Second call: find TEMPERATURE inside that sub-buffer. */
    r = Protocol_FindField(recordValue, recordLen, PROTO_FIELD_TEMPERATURE, &tempField, &tempFieldLen);
    assert(r == PROTO_OK);
    Protocol_GetU16(tempField, &decodedTemp);
    assert(decodedTemp == 305u);

    printf("PASS: FindField nested (two-call) lookup\n");
}

static void test_find_field_malformed(void)
{
    /* Claims a 10-byte value but the buffer only actually has 2 bytes of it. */
    uint8_t buf[5] = { PROTO_FIELD_TEMP_HIGH, 0x00u, 0x0Au, 0xAAu, 0xBBu };
    const uint8_t *found;
    uint16_t foundLen;
    ProtoResult_t r;

    r = Protocol_FindField(buf, sizeof(buf), PROTO_FIELD_TEMP_HIGH, &found, &foundLen);
    assert(r == PROTO_ERR_MALFORMED);

    printf("PASS: FindField malformed-buffer detection\n");
}

static void test_frame_roundtrip_no_stuffing(void)
{
    /* A payload with no bytes matching START/ESCAPE — the simple case. */
    uint8_t payload[4] = { 0x01u, 0x02u, 0x03u, 0x04u };
    uint8_t framed[32];
    uint16_t framedLen;
    uint8_t decoded[32];
    uint16_t decodedLen, consumed;
    ProtoResult_t r;

    r = Frame_Encode(payload, sizeof(payload), framed, sizeof(framed), &framedLen);
    assert(r == PROTO_OK);
    assert(framed[0] == PROTO_FRAME_START);
    assert(framedLen == (uint16_t)(1u + 2u + sizeof(payload) + 2u)); /* no stuffing needed here */

    r = Frame_Decode(framed, framedLen, decoded, sizeof(decoded), &decodedLen, &consumed);
    assert(r == PROTO_OK);
    assert(decodedLen == sizeof(payload));
    assert(consumed == framedLen);
    assert(memcmp(decoded, payload, sizeof(payload)) == 0);

    printf("PASS: Frame round-trip, no byte-stuffing needed\n");
}

static void test_frame_roundtrip_with_stuffing(void)
{
    /* A payload that deliberately contains START (0x7E) and ESCAPE (0x7D)
     * bytes, to prove byte-stuffing survives the round-trip. */
    uint8_t payload[5] = { 0x7Eu, 0x00u, 0x7Du, 0xFFu, 0x7Eu };
    uint8_t framed[32];
    uint16_t framedLen;
    uint8_t decoded[32];
    uint16_t decodedLen, consumed;
    ProtoResult_t r;

    r = Frame_Encode(payload, sizeof(payload), framed, sizeof(framed), &framedLen);
    assert(r == PROTO_OK);
    /* Every 0x7E/0x7D in the payload costs an extra byte once stuffed, plus
     * the un-stuffed START, 2-byte LENGTH, and 2-byte CRC (assumed unaffected
     * here, though the assertion below only checks the decoded round-trip). */
    assert(framedLen > (uint16_t)(1u + 2u + sizeof(payload) + 2u));

    r = Frame_Decode(framed, framedLen, decoded, sizeof(decoded), &decodedLen, &consumed);
    assert(r == PROTO_OK);
    assert(decodedLen == sizeof(payload));
    assert(consumed == framedLen);
    assert(memcmp(decoded, payload, sizeof(payload)) == 0);

    printf("PASS: Frame round-trip, with byte-stuffing\n");
}

static void test_frame_decode_malformed_crc(void)
{
    uint8_t payload[3] = { 0xAAu, 0xBBu, 0xCCu };
    uint8_t framed[32];
    uint16_t framedLen;
    uint8_t decoded[32];
    uint16_t decodedLen, consumed;
    ProtoResult_t r;

    Frame_Encode(payload, sizeof(payload), framed, sizeof(framed), &framedLen);
    framed[framedLen - 1] ^= 0xFFu; /* corrupt the last CRC byte */

    r = Frame_Decode(framed, framedLen, decoded, sizeof(decoded), &decodedLen, &consumed);
    assert(r == PROTO_ERR_MALFORMED);
    assert(consumed == framedLen); /* still reported, so the caller can resync past it */

    printf("PASS: Frame_Decode CRC-corruption detection\n");
}

static void test_frame_decode_incomplete(void)
{
    uint8_t payload[3] = { 0xAAu, 0xBBu, 0xCCu };
    uint8_t framed[32];
    uint16_t framedLen;
    uint8_t decoded[32];
    uint16_t decodedLen, consumed;
    ProtoResult_t r;

    Frame_Encode(payload, sizeof(payload), framed, sizeof(framed), &framedLen);

    /* Only hand over everything except the final CRC byte. */
    r = Frame_Decode(framed, (uint16_t)(framedLen - 1), decoded, sizeof(decoded), &decodedLen, &consumed);
    assert(r == PROTO_ERR_INCOMPLETE);

    /* No START byte at all yet. */
    r = Frame_Decode(payload, sizeof(payload), decoded, sizeof(decoded), &decodedLen, &consumed);
    assert(r == PROTO_ERR_INCOMPLETE);

    printf("PASS: Frame_Decode incomplete-buffer detection\n");
}

//int main(void)
//{
//    test_u16_roundtrip();
//    test_u32_roundtrip();
//    test_tlv_roundtrip_empty();
//    test_tlv_roundtrip_small_value();
//    test_decode_incomplete();
//    test_encode_buffer_too_small();
//    test_find_field();
//    test_find_field_nested();
//    test_find_field_malformed();
//    test_frame_roundtrip_no_stuffing();
//    test_frame_roundtrip_with_stuffing();
//    test_frame_decode_malformed_crc();
//    test_frame_decode_incomplete();
//
//    printf("\nAll protocol.c tests passed.\n");
//    return 0;
//}
