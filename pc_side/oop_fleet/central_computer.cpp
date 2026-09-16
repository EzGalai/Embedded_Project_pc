/*
 * central_computer.cpp — see central_computer.h.
 */

#include "central_computer.h"

#include "../central_computer/lnc_link_client.h"
#include "../central_computer/management_command.h"
#include "../central_computer/log.h"
#include "protocol.h"

#include <cstdio>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>

bool CentralComputer::Connect(const std::string &submarineId, const std::string &host, uint16_t port)
{
    if (fd_ >= 0) Disconnect();

    fd_ = CcCore_LncConnect(host.c_str(), port);
    if (fd_ < 0) return false;

    submarineId_ = submarineId;
    return true;
}

void CentralComputer::Disconnect()
{
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    haveLatest_ = false;
}

void CentralComputer::CacheMeasurementFields(const uint8_t *value, uint16_t valueLen)
{
    const uint8_t *field;
    uint16_t fieldLen;

    uint32_t timestamp = 0;
    if (Protocol_FindField(value, valueLen, PROTO_FIELD_TIMESTAMP, &field, &fieldLen) == PROTO_OK) {
        Protocol_GetU32(field, &timestamp);
    }

    const uint8_t *measurement;
    uint16_t measurementLen;
    if (Protocol_FindField(value, valueLen, PROTO_FIELD_MEASUREMENT_RECORD, &measurement, &measurementLen) != PROTO_OK) {
        return; /* e.g. an alarm/object-detection EVENT_REPORT with no sensor payload — leave the cache as-is */
    }

    latestTimestamp_ = timestamp;
    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_TEMPERATURE, &field, &fieldLen) == PROTO_OK) {
        uint16_t raw;
        Protocol_GetU16(field, &raw);
        latestTemperature_ = (int16_t)raw;
    }
    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_HUMIDITY, &field, &fieldLen) == PROTO_OK) {
        latestHumidity_ = field[0];
    }
    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_LIGHT, &field, &fieldLen) == PROTO_OK) {
        Protocol_GetU16(field, &latestLight_);
    }
    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_BATTERY_VOLTAGE, &field, &fieldLen) == PROTO_OK) {
        Protocol_GetU16(field, &latestBattery_);
    }
    if (Protocol_FindField(measurement, measurementLen, PROTO_FIELD_MODE, &field, &fieldLen) == PROTO_OK) {
        latestMode_ = field[0];
    }
    haveLatest_ = true;
}

void CentralComputer::PollLatestTelemetry()
{
    if (fd_ < 0) return;

    /* Cap on how many already-buffered messages one poll drains, in case
       several KEEP_ALIVEs queued up while the user was elsewhere in the
       menu — bounded so a poll still can't run away. */
    for (int i = 0; i < 5; ++i) {
        uint8_t prefix[4];
        ssize_t n = recv(fd_, prefix, sizeof(prefix), MSG_DONTWAIT);
        if (n <= 0) {
            if (n == 0) Disconnect(); /* peer closed cleanly */
            return; /* nothing (more) buffered right now, or a transient error — not a bug */
        }
        if (n < (ssize_t)sizeof(prefix)) {
            /* A length prefix split across two TCP segments is vanishingly
               unlikely for these tiny loopback-adjacent messages; rather
               than reassembling it, just skip this poll — the next one
               will catch up once the rest has arrived. */
            return;
        }

        uint32_t len;
        Protocol_GetU32(prefix, &len);

        uint8_t storage[256];
        if (len == 0 || len > sizeof(storage)) return;

        /* We've now committed to reading exactly `len` payload bytes — the
           stream is mid-frame. A real reply could occasionally take a
           moment, so this one read gets a short bounded wait rather than
           MSG_DONTWAIT; any failure past this point desyncs the framing,
           so it's treated as fatal (Disconnect) rather than silently
           misreading the next poll's prefix from leftover bytes. */
        struct timeval tv { 0, 500000 }; /* 500ms */
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ssize_t got = recv(fd_, storage, len, MSG_WAITALL);
        struct timeval noTimeout { 0, 0 };
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &noTimeout, sizeof(noTimeout));

        if (got != (ssize_t)len) {
            Disconnect();
            return;
        }

        uint8_t tag;
        const uint8_t *value;
        uint16_t valueLen, consumed;
        if (Protocol_DecodeTLV(storage, (uint16_t)len, &tag, &value, &valueLen, &consumed) != PROTO_OK) {
            continue;
        }

        if (tag == PROTO_TAG_KEEP_ALIVE) {
            CcCore_PrintKeepAlive(value, valueLen, submarineId_);
            CacheMeasurementFields(value, valueLen);
        } else if (tag == PROTO_TAG_EVENT_REPORT) {
            CcCore_PrintEventReport(value, valueLen, submarineId_);
            CacheMeasurementFields(value, valueLen);
        }
        /* Any other tag here would be a stray reply with nothing pending —
           shouldn't happen, since PollLatestTelemetry never sends a request. */
    }
}

void CentralComputer::QueryMeasurements(uint32_t startTime, uint32_t endTime)
{
    if (fd_ < 0) {
        printf("Not connected to an LNC.\n");
        return;
    }
    CcCore_GetMeasurements(fd_, startTime, endTime, submarineId_);
}

void CentralComputer::QueryEvents(uint32_t startTime, uint32_t endTime)
{
    if (fd_ < 0) {
        printf("Not connected to an LNC.\n");
        return;
    }
    CcCore_GetEvents(fd_, startTime, endTime, submarineId_);
}
