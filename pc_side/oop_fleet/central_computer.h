/**
 * @file central_computer.h
 * @brief A CombatSubmarine's own, genuine Central Computer (spec "OOP Part",
 * PROJECT_PLAN.md §4.19/§7/Phase 16) — a real wrapper around Part 1's
 * LNC-facing modules (lnc_link_client, management_command, log,
 * data_collection, all in pc_side/central_computer/), not a rewrite or a
 * simplified stand-in. Connects to its own lnc_bridge instance (its own
 * host/port — never the one central_computer's standalone process uses).
 *
 * Deliberately NOT used by any of the fleet's 10 menu operations (those
 * stay pure fleet/mission bookkeeping) — only by Fleet's mission
 * assign/end (which Connect/Disconnect it) and by ops 2/3/11 (which
 * display its cached telemetry or query its history). See PROJECT_PLAN.md's
 * Phase 16 scoping notes for why the two systems stay this separate.
 */
#ifndef CENTRAL_COMPUTER_H
#define CENTRAL_COMPUTER_H

#include <cstdint>
#include <string>

class CentralComputer {
public:
    CentralComputer() = default;
    ~CentralComputer() { Disconnect(); }

    /* Owns a raw socket fd — never copied. CombatSubmarine holds one by
       value but is itself only ever constructed in place (Fleet stores
       Submarines via unique_ptr), so this is never actually exercised;
       deleted anyway as the correct, honest contract for the resource. */
    CentralComputer(const CentralComputer &) = delete;
    CentralComputer &operator=(const CentralComputer &) = delete;

    /**
     * @brief Connects to this submarine's own lnc_bridge instance.
     * @param submarineId This submarine's serial number, for DCA storage.
     * @param host lnc_bridge host.
     * @param port lnc_bridge port.
     * @return true on success.
     */
    bool Connect(const std::string &submarineId, const std::string &host, uint16_t port);

    /** @brief Closes the connection, if any, and clears the cached telemetry. */
    void Disconnect();

    bool IsConnected() const { return fd_ >= 0; }

    /**
     * @brief Best-effort, non-blocking check for telemetry already sitting
     * in the socket buffer (KEEP_ALIVE / EVENT_REPORT are spontaneous —
     * there's no "give me your current status" request in the protocol).
     * Drains up to a handful of currently-buffered messages, printing and
     * DCA-storing each (reusing log.h, same as the standalone process),
     * and updates the cached latest-snapshot fields from whichever one
     * carried a MEASUREMENT_RECORD. A poll with nothing waiting is normal,
     * not an error — never blocks the caller for long.
     */
    void PollLatestTelemetry();

    bool HasLatestTelemetry() const { return haveLatest_; }
    uint32_t GetLatestTimestamp() const { return latestTimestamp_; }
    int16_t GetLatestTemperature() const { return latestTemperature_; }
    uint8_t GetLatestHumidity() const { return latestHumidity_; }
    uint16_t GetLatestLight() const { return latestLight_; }
    uint16_t GetLatestBattery() const { return latestBattery_; }
    uint8_t GetLatestMode() const { return latestMode_; }

    /** @brief Historical query — thin wrapper over management_command.h, prints results. */
    void QueryMeasurements(uint32_t startTime, uint32_t endTime);

    /** @brief Historical query — thin wrapper over management_command.h, prints results. */
    void QueryEvents(uint32_t startTime, uint32_t endTime);

private:
    void CacheMeasurementFields(const uint8_t *value, uint16_t valueLen);

    int fd_ = -1;
    std::string submarineId_;

    bool haveLatest_ = false;
    uint32_t latestTimestamp_ = 0;
    int16_t latestTemperature_ = 0;
    uint8_t latestHumidity_ = 0;
    uint16_t latestLight_ = 0;
    uint16_t latestBattery_ = 0;
    uint8_t latestMode_ = 0;
};

#endif
