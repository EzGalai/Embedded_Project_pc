/*
 * management_command.cpp — see management_command.h.
 */

#include "management_command.h"
#include "lnc_link_client.h"
#include "protocol.h"

#include <cstdio>

bool CcCore_GetTime(int fd, uint32_t *outTime)
{
    CcCore_LncSend(fd, PROTO_TAG_GET_TIME_REQ, nullptr, 0);

    uint8_t storage[256];
    uint8_t tag;
    const uint8_t *value;
    uint16_t valueLen;

    if (!CcCore_LncRecvMessage(fd, &tag, &value, &valueLen, storage, sizeof(storage))) {
        return false;
    }
    if (tag != PROTO_TAG_GET_TIME_RESP || valueLen < 4) {
        fprintf(stderr, "central_computer: expected GET_TIME_RESP, got tag 0x%02X\n", tag);
        return false;
    }

    Protocol_GetU32(value, outTime);
    return true;
}

bool CcCore_SetRtc(int fd, uint32_t newTime, ProtoStatus_t *outStatus)
{
    uint8_t valueBuf[4];
    Protocol_PutU32(valueBuf, newTime);
    CcCore_LncSend(fd, PROTO_TAG_SET_RTC_REQ, valueBuf, 4);

    uint8_t storage[256];
    uint8_t tag;
    const uint8_t *value;
    uint16_t valueLen;

    if (!CcCore_LncRecvMessage(fd, &tag, &value, &valueLen, storage, sizeof(storage))) {
        return false;
    }
    if (tag != PROTO_TAG_CONFIG_ACK || valueLen < 1) {
        fprintf(stderr, "central_computer: expected CONFIG_ACK, got tag 0x%02X\n", tag);
        return false;
    }

    *outStatus = (ProtoStatus_t)value[0];
    return true;
}

bool CcCore_SetBatteryWarningMin(int fd, uint16_t newMinMv, ProtoStatus_t *outStatus)
{
    uint8_t fieldBuf[2];
    Protocol_PutU16(fieldBuf, newMinMv);

    uint8_t value[8];
    uint16_t valueLen;
    Protocol_EncodeTLV(PROTO_FIELD_BATTERY_MIN, fieldBuf, 2, value, sizeof(value), &valueLen);

    CcCore_LncSend(fd, PROTO_TAG_SET_BATTERY_WARNING_MIN, value, valueLen);

    uint8_t storage[256];
    uint8_t tag;
    const uint8_t *respValue;
    uint16_t respValueLen;
    if (!CcCore_LncRecvMessage(fd, &tag, &respValue, &respValueLen, storage, sizeof(storage))) {
        return false;
    }
    if (tag != PROTO_TAG_CONFIG_ACK || respValueLen < 1) {
        fprintf(stderr, "central_computer: expected CONFIG_ACK, got tag 0x%02X\n", tag);
        return false;
    }

    *outStatus = (ProtoStatus_t)respValue[0];
    return true;
}

void CcCore_GetMeasurements(int fd, uint32_t startTime, uint32_t endTime)
{
    printf("GET_MEASUREMENTS(%u, %u):\n", (unsigned)startTime, (unsigned)endTime);

    bool haveCursor = false;
    uint32_t cursorDay = 0, cursorOffset = 0;
    int totalRecords = 0;

    for (;;) {
        uint8_t valueBuf[4];
        uint8_t payload[32];
        uint16_t payloadLen = 0, written;

        Protocol_PutU32(valueBuf, startTime);
        Protocol_EncodeTLV(PROTO_FIELD_TIME_RANGE_START, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
        payloadLen = (uint16_t)(payloadLen + written);

        Protocol_PutU32(valueBuf, endTime);
        Protocol_EncodeTLV(PROTO_FIELD_TIME_RANGE_END, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
        payloadLen = (uint16_t)(payloadLen + written);

        if (haveCursor) {
            Protocol_PutU32(valueBuf, cursorDay);
            Protocol_EncodeTLV(PROTO_FIELD_CURSOR_DAY, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
            payloadLen = (uint16_t)(payloadLen + written);

            Protocol_PutU32(valueBuf, cursorOffset);
            Protocol_EncodeTLV(PROTO_FIELD_CURSOR_OFFSET, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
            payloadLen = (uint16_t)(payloadLen + written);
        }

        CcCore_LncSend(fd, PROTO_TAG_GET_MEASUREMENTS_REQ, payload, payloadLen);

        uint8_t storage[300];
        uint8_t tag;
        const uint8_t *value;
        uint16_t valueLen;

        if (!CcCore_LncRecvMessage(fd, &tag, &value, &valueLen, storage, sizeof(storage))) {
            fprintf(stderr, "central_computer: GET_MEASUREMENTS_RESP not received\n");
            return;
        }
        if (tag != PROTO_TAG_GET_MEASUREMENTS_RESP) {
            fprintf(stderr, "central_computer: expected GET_MEASUREMENTS_RESP, got tag 0x%02X\n", tag);
            return;
        }

        const uint8_t *field;
        uint16_t fieldLen;
        uint8_t status = 0;
        if (Protocol_FindField(value, valueLen, PROTO_FIELD_STATUS, &field, &fieldLen) == PROTO_OK) {
            status = field[0];
        }

        if (status == PROTO_STATUS_NO_DATA_FOUND) {
            printf("  no data found in range\n");
            return;
        }
        if (status != PROTO_STATUS_SUCCESS) {
            printf("  status=%u\n", status);
            return;
        }

        /* Protocol_FindField only returns the first match of a tag — since
           a response can carry multiple MEASUREMENT_RECORD fields, walk
           the value buffer manually (tag+len+value, advancing by 3+len). */
        bool responseHasCursor = false;
        uint16_t offset = 0;
        while ((uint16_t)(offset + 3) <= valueLen) {
            uint8_t fTag = value[offset];
            uint16_t fLen;
            Protocol_GetU16(value + offset + 1, &fLen);
            const uint8_t *fValue = value + offset + 3;

            if (fTag == PROTO_FIELD_MEASUREMENT_RECORD) {
                uint32_t ts = 0;
                int16_t temperature = 0;
                uint8_t humidity = 0, mode = 0;
                uint16_t light = 0, battery = 0;
                const uint8_t *mf;
                uint16_t mfLen;

                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_TIMESTAMP, &mf, &mfLen) == PROTO_OK) Protocol_GetU32(mf, &ts);
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_TEMPERATURE, &mf, &mfLen) == PROTO_OK) { uint16_t raw; Protocol_GetU16(mf, &raw); temperature = (int16_t)raw; }
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_HUMIDITY, &mf, &mfLen) == PROTO_OK) humidity = mf[0];
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_LIGHT, &mf, &mfLen) == PROTO_OK) Protocol_GetU16(mf, &light);
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_BATTERY_VOLTAGE, &mf, &mfLen) == PROTO_OK) Protocol_GetU16(mf, &battery);
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_MODE, &mf, &mfLen) == PROTO_OK) mode = mf[0];

                char timeStr[32];
                FormatUnixTime(ts, timeStr, sizeof(timeStr));
                printf("  [%s] temp=%.1fC humidity=%u%% light=%u%% battery=%u%% mode=%u\n",
                       timeStr, temperature / 10.0, humidity,
                       (unsigned)(light * 100 / 4095), (unsigned)(battery * 100 / 3300), mode);
                totalRecords++;
            } else if (fTag == PROTO_FIELD_CURSOR_DAY) {
                Protocol_GetU32(fValue, &cursorDay);
                responseHasCursor = true;
            } else if (fTag == PROTO_FIELD_CURSOR_OFFSET) {
                Protocol_GetU32(fValue, &cursorOffset);
            }

            offset = (uint16_t)(offset + 3 + fLen);
        }

        if (!responseHasCursor) {
            printf("GET_MEASUREMENTS: done, %d record(s) total\n", totalRecords);
            return;
        }
        haveCursor = true;
    }
}

void CcCore_GetEvents(int fd, uint32_t startTime, uint32_t endTime)
{
    printf("GET_EVENTS(%u, %u):\n", (unsigned)startTime, (unsigned)endTime);

    bool haveCursor = false;
    uint32_t cursorDay = 0, cursorOffset = 0;
    int totalRecords = 0;

    for (;;) {
        uint8_t valueBuf[4];
        uint8_t payload[32];
        uint16_t payloadLen = 0, written;

        Protocol_PutU32(valueBuf, startTime);
        Protocol_EncodeTLV(PROTO_FIELD_TIME_RANGE_START, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
        payloadLen = (uint16_t)(payloadLen + written);

        Protocol_PutU32(valueBuf, endTime);
        Protocol_EncodeTLV(PROTO_FIELD_TIME_RANGE_END, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
        payloadLen = (uint16_t)(payloadLen + written);

        if (haveCursor) {
            Protocol_PutU32(valueBuf, cursorDay);
            Protocol_EncodeTLV(PROTO_FIELD_CURSOR_DAY, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
            payloadLen = (uint16_t)(payloadLen + written);

            Protocol_PutU32(valueBuf, cursorOffset);
            Protocol_EncodeTLV(PROTO_FIELD_CURSOR_OFFSET, valueBuf, 4, payload + payloadLen, (uint16_t)(sizeof(payload) - payloadLen), &written);
            payloadLen = (uint16_t)(payloadLen + written);
        }

        CcCore_LncSend(fd, PROTO_TAG_GET_EVENTS_REQ, payload, payloadLen);

        uint8_t storage[300];
        uint8_t tag;
        const uint8_t *value;
        uint16_t valueLen;

        if (!CcCore_LncRecvMessage(fd, &tag, &value, &valueLen, storage, sizeof(storage))) {
            fprintf(stderr, "central_computer: GET_EVENTS_RESP not received\n");
            return;
        }
        if (tag != PROTO_TAG_GET_EVENTS_RESP) {
            fprintf(stderr, "central_computer: expected GET_EVENTS_RESP, got tag 0x%02X\n", tag);
            return;
        }

        const uint8_t *field;
        uint16_t fieldLen;
        uint8_t status = 0;
        if (Protocol_FindField(value, valueLen, PROTO_FIELD_STATUS, &field, &fieldLen) == PROTO_OK) {
            status = field[0];
        }

        if (status == PROTO_STATUS_NO_DATA_FOUND) {
            printf("  no data found in range\n");
            return;
        }
        if (status != PROTO_STATUS_SUCCESS) {
            printf("  status=%u\n", status);
            return;
        }

        /* Protocol_FindField only returns the first match of a tag — since
           a response can carry multiple EVENT_RECORD fields, walk the
           value buffer manually (tag+len+value, advancing by 3+len). */
        bool responseHasCursor = false;
        uint16_t offset = 0;
        while ((uint16_t)(offset + 3) <= valueLen) {
            uint8_t fTag = value[offset];
            uint16_t fLen;
            Protocol_GetU16(value + offset + 1, &fLen);
            const uint8_t *fValue = value + offset + 3;

            if (fTag == PROTO_FIELD_EVENT_RECORD) {
                uint32_t ts = 0;
                uint8_t eventType = 0, eventSource = 0;
                const uint8_t *ef;
                uint16_t efLen;

                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_TIMESTAMP, &ef, &efLen) == PROTO_OK) Protocol_GetU32(ef, &ts);
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_EVENT_TYPE, &ef, &efLen) == PROTO_OK) eventType = ef[0];
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_EVENT_SOURCE, &ef, &efLen) == PROTO_OK) eventSource = ef[0];

                char timeStr[32];
                FormatUnixTime(ts, timeStr, sizeof(timeStr));
                printf("  [%s] type=%u source=%u", timeStr, eventType, eventSource);

                const uint8_t *measurement;
                uint16_t measurementLen;
                if (Protocol_FindField(fValue, fLen, PROTO_FIELD_MEASUREMENT_RECORD, &measurement, &measurementLen) == PROTO_OK) {
                    int16_t temperature = 0;
                    uint8_t humidity = 0, mode = 0;
                    uint16_t light = 0, battery = 0;

                    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_TEMPERATURE, &ef, &efLen) == PROTO_OK) { uint16_t raw; Protocol_GetU16(ef, &raw); temperature = (int16_t)raw; }
                    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_HUMIDITY, &ef, &efLen) == PROTO_OK) humidity = ef[0];
                    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_LIGHT, &ef, &efLen) == PROTO_OK) Protocol_GetU16(ef, &light);
                    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_BATTERY_VOLTAGE, &ef, &efLen) == PROTO_OK) Protocol_GetU16(ef, &battery);
                    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_MODE, &ef, &efLen) == PROTO_OK) mode = ef[0];

                    printf(" | temp=%.1fC humidity=%u%% light=%u%% battery=%u%% mode=%u",
                           temperature / 10.0, humidity, (unsigned)(light * 100 / 4095),
                           (unsigned)(battery * 100 / 3300), mode);
                }
                printf("\n");
                totalRecords++;
            } else if (fTag == PROTO_FIELD_CURSOR_DAY) {
                Protocol_GetU32(fValue, &cursorDay);
                responseHasCursor = true;
            } else if (fTag == PROTO_FIELD_CURSOR_OFFSET) {
                Protocol_GetU32(fValue, &cursorOffset);
            }

            offset = (uint16_t)(offset + 3 + fLen);
        }

        if (!responseHasCursor) {
            printf("GET_EVENTS: done, %d record(s) total\n", totalRecords);
            return;
        }
        haveCursor = true;
    }
}
