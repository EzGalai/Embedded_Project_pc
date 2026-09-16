/**
 * @file submarine.h
 * @brief Abstract base of the OOP Fleet Management System's submarine
 * hierarchy (PROJECT_PLAN.md §4.19/§7 — "OOP Part" of the spec). This is a
 * pure in-memory C++ model: no networking, no persistence, deliberately
 * separate from Part 1's live LNC/CentralComputer/GroundStation system (see
 * combat_submarine.h's note on the embedded CentralComputer).
 */
#ifndef SUBMARINE_H
#define SUBMARINE_H

#include <string>

/**
 * @brief Prompts and reads one line of text (may be empty).
 */
std::string PromptLine(const std::string &prompt);

/**
 * @brief Prompts and reads one integer, reprompting until the input parses.
 */
int PromptInt(const std::string &prompt);

/**
 * @brief Common fields/behavior for every submarine in the fleet:
 * serial number, name, and whether it's currently assigned to a mission.
 * Type-specific mission content (research topic vs. combat mission) is
 * left to each derived class via the pure virtuals below.
 */
class Submarine {
public:
    Submarine(std::string serialNumber, std::string name);
    virtual ~Submarine() = default;

    const std::string &GetSerialNumber() const;
    const std::string &GetName() const;
    bool IsAssignedToMission() const;

    /** @brief Human-readable type name for display ("Research"/"Combat"). */
    virtual std::string GetTypeName() const = 0;

    /** @brief Prompts for this submarine's type-specific mission details and marks it assigned. */
    virtual void AssignMission() = 0;

    /** @brief Prompts for and updates this submarine's type-specific mission details. */
    virtual void UpdateMissionDetails() = 0;

    /**
     * @brief Ends this submarine's current mission. Overrides must clear
     * their own type-specific mission fields, then call
     * Submarine::EndMission() to clear the common flag.
     */
    virtual void EndMission();

    /** @brief Prints this submarine's details, including type-specific ones. */
    virtual void Display() const = 0;

protected:
    void SetAssigned(bool assigned);

private:
    std::string serialNumber_;
    std::string name_;
    bool assignedToMission_;
};

#endif
