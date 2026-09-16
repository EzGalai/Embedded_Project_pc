/*
 * log.cpp — see log.h.
 */

#include "log.h"
#include "lnc_link_client.h"
#include "data_collection.h"
#include "protocol.h"

#include <cstdio>

void CcCore_PrintKeepAlive(const uint8_t *value, uint16_t len, const std::string &submarineId)
{
    const uint8_t *field;
    uint16_t fieldLen;

    uint32_t timestamp = 0;
    if (Protocol_FindField(value, len, PROTO_FIELD_TIMESTAMP, &field, &fieldLen) == PROTO_OK) {
        Protocol_GetU32(field, &timestamp);
    }

    uint8_t mode = 0;
    if (Protocol_FindField(value, len, PROTO_FIELD_MODE, &field, &fieldLen) == PROTO_OK) {
        mode = field[0];
    }

    char timeStr[32];
    FormatUnixTime(timestamp, timeStr, sizeof(timeStr));

    const uint8_t *measurement;
    uint16_t measurementLen;
    if (Protocol_FindField(value, len, PROTO_FIELD_MEASUREMENT_RECORD, &measurement, &measurementLen) != PROTO_OK) {
        printf("KEEP_ALIVE: time=%s mode=%u (no MEASUREMENT_RECORD found)\n", timeStr, mode);
        return;
    }

    int16_t temperature = 0;
    uint8_t humidity = 0;
    uint16_t light = 0, battery = 0;

    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_TEMPERATURE, &field, &fieldLen) == PROTO_OK) {
        uint16_t raw;
        Protocol_GetU16(field, &raw);
        temperature = (int16_t)raw;
    }
    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_HUMIDITY, &field, &fieldLen) == PROTO_OK) {
        humidity = field[0];
    }
    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_LIGHT, &field, &fieldLen) == PROTO_OK) {
        Protocol_GetU16(field, &light);
    }
    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_BATTERY_VOLTAGE, &field, &fieldLen) == PROTO_OK) {
        Protocol_GetU16(field, &battery);
    }

    DCA_StoreMeasurement(submarineId, timestamp, temperature, humidity, light, battery, mode);

    printf("KEEP_ALIVE: time=%s mode=%u | temp=%.1fC humidity=%u%% light=%u%% battery=%u%%\n",
       timeStr, mode, temperature / 10.0, humidity,
       (unsigned)(light * 100 / 4095), (unsigned)(battery * 100 / 3300));
}

void CcCore_PrintEventReport(const uint8_t *value, uint16_t len, const std::string &submarineId)
{
    const uint8_t *field;
    uint16_t fieldLen;

    uint32_t timestamp = 0;
    if (Protocol_FindField(value, len, PROTO_FIELD_TIMESTAMP, &field, &fieldLen) == PROTO_OK) {
        Protocol_GetU32(field, &timestamp);
    }

    uint8_t eventType = 0, eventSource = 0;
    if (Protocol_FindField(value, len, PROTO_FIELD_EVENT_TYPE, &field, &fieldLen) == PROTO_OK) {
        eventType = field[0];
    }
    if (Protocol_FindField(value, len, PROTO_FIELD_EVENT_SOURCE, &field, &fieldLen) == PROTO_OK) {
        eventSource = field[0];
    }

    char timeStr[32];
    FormatUnixTime(timestamp, timeStr, sizeof(timeStr));

    printf("EVENT_REPORT: time=%s type=%u source=%u", timeStr, eventType, eventSource);

    if (Protocol_FindField(value, len, PROTO_FIELD_WD_RESET_FLAG, &field, &fieldLen) == PROTO_OK) {
        printf(" wd_reset=%u", field[0]);
    }

    const uint8_t *measurement;
    uint16_t measurementLen;
    if (Protocol_FindField(value, len, PROTO_FIELD_MEASUREMENT_RECORD, &measurement, &measurementLen) == PROTO_OK) {
        int16_t temperature = 0;
        uint8_t humidity = 0, mode = 0;
        uint16_t light = 0, battery = 0;

        if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_TEMPERATURE, &field, &fieldLen) == PROTO_OK) {
            uint16_t raw; Protocol_GetU16(field, &raw); temperature = (int16_t)raw;
        }
        if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_HUMIDITY, &field, &fieldLen) == PROTO_OK) {
            humidity = field[0];
        }
        if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_LIGHT, &field, &fieldLen) == PROTO_OK) {
            Protocol_GetU16(field, &light);
        }
        if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_BATTERY_VOLTAGE, &field, &fieldLen) == PROTO_OK) {
            Protocol_GetU16(field, &battery);
        }
        if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_MODE, &field, &fieldLen) == PROTO_OK) {
            mode = field[0];
        }

        DCA_StoreEvent(submarineId, timestamp, eventType, eventSource, true, temperature, humidity, light, battery, mode);

        printf(" | temp=%.1fC humidity=%u%% light=%u%% battery=%u%% mode=%u",
               temperature / 10.0, humidity, (unsigned)(light * 100 / 4095),
               (unsigned)(battery * 100 / 3300), mode);
    } else {
        DCA_StoreEvent(submarineId, timestamp, eventType, eventSource, false, 0, 0, 0, 0, 0);
    }
    printf("\n");
}
