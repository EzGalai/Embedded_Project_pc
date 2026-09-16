/**
 * @file management_command.h
 * @brief Request/response helpers for the LNC's Management-command set
 * (GET_TIME, SET_RTC, SET_BATTERY_WARNING_MIN, GET_MEASUREMENTS, GET_EVENTS)
 * — PROJECT_PLAN.md §7's planned filename for this concern. Built on
 * lnc_link_client.h's raw send/receive primitives.
 */
#ifndef MANAGEMENT_COMMAND_H
#define MANAGEMENT_COMMAND_H

#include "protocol.h"

#include <cstdint>

/**
 * @brief Sends GET_TIME_REQ and waits for GET_TIME_RESP.
 * @param fd Connected socket.
 * @param outTime Set to the decoded TIMESTAMP on success.
 * @return true on success.
 */
bool CcCore_GetTime(int fd, uint32_t *outTime);

/**
 * @brief Sends SET_RTC_REQ with newTime and waits for CONFIG_ACK.
 * @param fd Connected socket.
 * @param newTime Timestamp to set.
 * @param outStatus Set to the ACK's STATUS on success.
 * @return true on success.
 */
bool CcCore_SetRtc(int fd, uint32_t newTime, ProtoStatus_t *outStatus);

/**
 * @brief Sends SET_BATTERY_WARNING_MIN with newMinMv and waits for CONFIG_ACK.
 * Phase 12 test: proves a SET_* config command reaches Config_ApplyUpdate
 * and gets acknowledged.
 * @param fd Connected socket.
 * @param newMinMv New battery-warning-minimum threshold, in mV.
 * @param outStatus Set to the ACK's STATUS on success.
 * @return true on success.
 */
bool CcCore_SetBatteryWarningMin(int fd, uint16_t newMinMv, ProtoStatus_t *outStatus);

/**
 * @brief Sends GET_MEASUREMENTS_REQ for [startTime, endTime] and prints
 * every returned MEASUREMENT_RECORD, automatically following the
 * CURSOR_DAY/CURSOR_OFFSET pagination protocol (Phase 13) until a response
 * arrives with no cursor attached, meaning the whole range has been sent.
 * @param fd Connected socket.
 * @param startTime Inclusive range start (Unix timestamp).
 * @param endTime Inclusive range end (Unix timestamp).
 */
void CcCore_GetMeasurements(int fd, uint32_t startTime, uint32_t endTime);

/**
 * @brief Sends GET_EVENTS_REQ for [startTime, endTime] and prints every
 * returned EVENT_RECORD (including the nested MEASUREMENT_RECORD carried
 * by Monitor-sourced events), automatically following the
 * CURSOR_DAY/CURSOR_OFFSET pagination protocol until a response arrives
 * with no cursor attached — same mechanism as CcCore_GetMeasurements.
 * @param fd Connected socket.
 * @param startTime Inclusive range start (Unix timestamp).
 * @param endTime Inclusive range end (Unix timestamp).
 */
void CcCore_GetEvents(int fd, uint32_t startTime, uint32_t endTime);

#endif
