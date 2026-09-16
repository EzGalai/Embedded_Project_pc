/**
 * @file comm_gs.h
 * @brief Ground Station-facing TCP server (PROJECT_PLAN.md §7): handshake,
 * GS_GET_LOG_REQ/GS_GET_EVENTS_REQ dispatch, backed by data_collection.h's
 * stored measurements/events. Runs on its own thread (Phase 15) — see
 * CC_GsLink_Run.
 */
#ifndef COMM_GS_H
#define COMM_GS_H

#include <cstdint>

#define GS_TCP_PORT 9000

/**
 * @brief Runs forever on its own thread (Phase 15): binds/listens once,
 * then repeatedly accepts a GS client, performs the handshake, serves
 * GS_GET_LOG_REQ/GS_GET_EVENTS_REQ until that client disconnects, and goes
 * back to accepting the next one. Runs concurrently with the main thread's
 * LNC handling — see data_collection.cpp's mutex for the resulting
 * thread-safety requirement on DCA's file operations.
 * @param port Port to listen on.
 */
void CC_GsLink_Run(uint16_t port);

#endif
