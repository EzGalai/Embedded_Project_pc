/**
 * @file combat_submarine.h
 * @brief Combat submarine: used for operational missions, tracking its
 * mission description, commander, personnel count, the other combat
 * submarines participating in its current mission, its mission history,
 * and messages received from other combat submarines (spec "OOP Part").
 */
#ifndef COMBAT_SUBMARINE_H
#define COMBAT_SUBMARINE_H

#include "submarine.h"
#include "central_computer.h"

#include <string>
#include <vector>

/**
 * @brief One message sent between CombatSubmarines participating in the
 * same mission (spec "OOP Part", menu operations 8/9).
 */
struct Message {
    std::string content;
    std::string senderSerialNumber;
};

class CombatSubmarine : public Submarine {
public:
    CombatSubmarine(std::string serialNumber, std::string name);

    std::string GetTypeName() const override;
    void AssignMission() override;
    void UpdateMissionDetails() override;

    /**
     * @brief Clears this submarine's own mission fields and archives the
     * ended mission into missionHistory_. Does NOT unlink this submarine
     * from other CombatSubmarines' participatingSubmarines_ lists — that
     * symmetric cleanup needs fleet-wide lookup and is Fleet::EndMission's
     * responsibility.
     */
    void EndMission() override;

    void Display() const override;

    /** @brief For Fleet's historical-data menu option (op 11) — this submarine's live LNC link, if any. */
    CentralComputer &GetCentralComputer();

    /** @brief Adds otherSerial to this submarine's participating list (idempotent, ignores self). */
    void AddParticipant(const std::string &otherSerial);

    /** @brief Removes otherSerial from this submarine's participating list, if present. */
    void RemoveParticipant(const std::string &otherSerial);

    bool HasParticipant(const std::string &otherSerial) const;

    void ReceiveMessage(const std::string &senderSerial, const std::string &content);

    const std::vector<Message> &GetReceivedMessages() const;
    const std::vector<std::string> &GetParticipatingSubmarines() const;

private:
    std::string missionDescription_;
    std::string commanderName_;
    int personnelCount_;
    /* mutable: Display() is logically read-only but refreshes this
       best-effort telemetry cache as a side effect — same "mutable cache
       behind a const accessor" pattern as any other memoizing const method. */
    mutable CentralComputer centralComputer_;
    std::vector<std::string> participatingSubmarines_;
    std::vector<std::string> missionHistory_;
    std::vector<Message> receivedMessages_;
};

#endif
