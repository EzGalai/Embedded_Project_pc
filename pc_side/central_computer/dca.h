/**
 * @file dca.h
 * @brief Data Collection & Analysis — central_computer's own persistent
 * storage. See PROJECT_PLAN.md §4.17. One directory per submarine (keyed
 * by a submarine ID — a fixed "LNC-01" constant for this project, since a
 * real per-connection ID isn't wired into the live traffic until Phase 16's
 * Fleet Management work), date-named files, 7-day retention — the same
 * mental model as the LNC's own Log module (§4.5), just on the PC side.
 */
#ifndef DCA_H
#define DCA_H

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief One parsed measurement record, as returned by DCA_QueryMeasurements.
 */
struct DcaMeasurement {
    uint32_t timestamp = 0;
    int16_t temperature = 0;
    uint8_t humidity = 0;
    uint16_t light = 0;
    uint16_t batteryVoltage = 0;
    uint8_t mode = 0;
};

/**
 * @brief One parsed event record, as returned by DCA_QueryEvents.
 */
struct DcaEvent {
    uint32_t timestamp = 0;
    uint8_t eventType = 0;
    uint8_t eventSource = 0;
    bool hasMeasurement = false;
    DcaMeasurement measurement; /* valid only if hasMeasurement */
};


/**
 * @brief Appends one line to today's measurement log for a submarine
 * (data/<submarineId>/measurements/YYYY-MM-DD.log). The file is dated by
 * this machine's own system clock, not the submarine's reported
 * timestamp — the LNC's RTC resets to the same baseline on every reboot,
 * so dating by receipt time keeps storage cleanly chronological
 * regardless of how many times the LNC has been rebooted. Rotates
 * (deletes the oldest file once more than 7 exist) on the first write of
 * a new day.
 * @param submarineId Identifies which submarine's directory to store under.
 * @param timestamp Submarine-reported Unix timestamp (stored as a field, not used for dating/rotation).
 * @param temperature Tenths of a degree C (matches the wire's TEMPERATURE field).
 * @param humidity Percent.
 * @param light Raw ADC reading.
 * @param batteryVoltage Millivolts.
 * @param mode 0=Normal, 1=Warning, 2=Error.
 */
void DCA_StoreMeasurement(const std::string &submarineId, uint32_t timestamp, int16_t temperature,
                           uint8_t humidity, uint16_t light, uint16_t batteryVoltage, uint8_t mode);

/**
 * @brief Appends one line to today's event log for a submarine
 * (data/<submarineId>/events/YYYY-MM-DD.log). Same dating/rotation rule
 * as DCA_StoreMeasurement. hasMeasurement selects which of the two line
 * shapes gets written — mirrors the LNC's own log.c/Log_WriteEvent.
 * @param hasMeasurement True for a Monitor-sourced event (mode change),
 * false for an Object-Detection event (no measurement snapshot).
 */
void DCA_StoreEvent(const std::string &submarineId, uint32_t timestamp, uint8_t eventType, uint8_t eventSource,
                     bool hasMeasurement, int16_t temperature, uint8_t humidity, uint16_t light,
                     uint16_t batteryVoltage, uint8_t mode);

/**
 * @brief Scans every stored measurement file for a submarine (not just
 * date-matched ones — a record's reported timestamp doesn't necessarily
 * correlate with which file it landed in, since files are dated by
 * receipt time) and returns every line whose own timestamp field falls
 * within [start, end]. No pagination — a single in-memory result is fine
 * over a plain TCP link at this project's scale.
 * @param submarineId Which submarine's directory to scan.
 * @param start Inclusive range start (Unix timestamp).
 * @param end Inclusive range end (Unix timestamp).
 * @return Matching records, in file order.
 */
std::vector<DcaMeasurement> DCA_QueryMeasurements(const std::string &submarineId, uint32_t start, uint32_t end);

/**
 * @brief Same as DCA_QueryMeasurements, for events.
 */
std::vector<DcaEvent> DCA_QueryEvents(const std::string &submarineId, uint32_t start, uint32_t end);

#endif
