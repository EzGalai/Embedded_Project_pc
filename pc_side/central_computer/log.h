/**
 * @file log.h
 * @brief Processes and prints incoming telemetry from the LNC (KEEP_ALIVE,
 * EVENT_REPORT), persisting each via data_collection.h's DCA_Store*
 * functions. PROJECT_PLAN.md §7's planned filename for this concern.
 */
#ifndef LOG_H
#define LOG_H

#include <cstdint>

/**
 * @brief Parses one KEEP_ALIVE's fields, prints them, and persists the
 * measurement via DCA_StoreMeasurement.
 * @param value KEEP_ALIVE message's Value.
 * @param len Length of value.
 */
void CcCore_PrintKeepAlive(const uint8_t *value, uint16_t len);

/**
 * @brief Parses one EVENT_REPORT's fields, prints them, and persists it
 * via DCA_StoreEvent.
 * @param value EVENT_REPORT message's Value.
 * @param len Length of value.
 */
void CcCore_PrintEventReport(const uint8_t *value, uint16_t len);

#endif
