/**
 * @file fleet.h
 * @brief Container of the fleet's submarines, the 10 menu operations
 * (spec "OOP Part"), and one extra operation (11) for querying a connected
 * CombatSubmarine's live LNC history. In-memory only for this run — the
 * spec doesn't ask fleet/mission data to survive restarts, unlike Part 1's
 * sensor data.
 */
#ifndef FLEET_H
#define FLEET_H

#include "submarine.h"

#include <memory>
#include <string>
#include <vector>

class Fleet {
public:
    /** @brief Menu op 1: choose a type, enter its details, add it to the fleet. */
    void AddSubmarine();

    /** @brief Menu op 2: display every submarine's details and mission status. */
    void DisplayAll() const;

    /** @brief Menu op 3: search for and display one submarine by serial number. */
    void SearchAndDisplay() const;

    /** @brief Menu op 4: assign a mission to a submarine, entering its type-specific details. */
    void AssignMission();

    /** @brief Menu op 5: update a submarine's mission details, per its type. */
    void UpdateMissionDetails();

    /**
     * @brief Menu op 6: end a submarine's mission. For a CombatSubmarine,
     * also removes it from every other CombatSubmarine's participating
     * list — the symmetric half of AssociateCombatSubmarines' linking,
     * which only CombatSubmarine::EndMission itself can't do (it has no
     * visibility into the rest of the fleet).
     */
    void EndMission();

    /** @brief Menu op 7: associate two combat submarines as participating in the same mission (symmetric). */
    void AssociateCombatSubmarines();

    /** @brief Menu op 8: send a message from one combat submarine to another participating in the same mission. */
    void SendMessage();

    /** @brief Menu op 9: display the messages received by a submarine. */
    void DisplayReceivedMessages() const;

    /**
     * @brief Extra op, past the spec's 10: query a connected
     * CombatSubmarine's central computer for historical measurements/events
     * over a chosen hours-back range.
     */
    void ViewHistoricalData();

private:
    Submarine *FindBySerial(const std::string &serial) const;

    std::vector<std::unique_ptr<Submarine>> submarines_;
};

#endif
