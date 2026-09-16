/**
 * @file gs_queries.h
 * @brief Ground Station's real queries against central_computer's GS-facing
 * link (Phase 14, PROJECT_PLAN.md §5.4): GS_GET_LOG_REQ/RESP and
 * GS_GET_EVENTS_REQ/RESP. Split out of main.cpp for readability — main.cpp
 * keeps only the connection/handshake, this file owns the actual query
 * logic.
 */
#ifndef GS_QUERIES_H
#define GS_QUERIES_H

#include <cstdint>
#include <string>

/**
 * @brief Sends GS_GET_LOG_REQ(submarineId, start, end) and prints every
 * returned MEASUREMENT_RECORD. No pagination on this link — the whole
 * result arrives in one response.
 * @param fd Connected socket to central_computer's GS link.
 * @param submarineId Which submarine's data to query.
 * @param start Inclusive range start (Unix timestamp).
 * @param end Inclusive range end (Unix timestamp).
 */
void Gs_RequestLog(int fd, const std::string &submarineId, uint32_t start, uint32_t end);

/**
 * @brief Sends GS_GET_EVENTS_REQ(submarineId, start, end) and prints every
 * returned EVENT_RECORD (including its nested MEASUREMENT_RECORD, for
 * Monitor-sourced events). Same no-pagination shape as Gs_RequestLog.
 * @param fd Connected socket to central_computer's GS link.
 * @param submarineId Which submarine's data to query.
 * @param start Inclusive range start (Unix timestamp).
 * @param end Inclusive range end (Unix timestamp).
 */
void Gs_RequestEvents(int fd, const std::string &submarineId, uint32_t start, uint32_t end);

#endif
