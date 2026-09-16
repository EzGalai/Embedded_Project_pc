/*
 * submarine.cpp — see submarine.h.
 */

#include "submarine.h"

#include <iostream>

std::string PromptLine(const std::string &prompt)
{
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    return line;
}

int PromptInt(const std::string &prompt)
{
    for (;;) {
        std::string line = PromptLine(prompt);
        try {
            return std::stoi(line);
        } catch (...) {
            std::cout << "  Please enter a valid number.\n";
        }
    }
}

Submarine::Submarine(std::string serialNumber, std::string name)
    : serialNumber_(std::move(serialNumber)), name_(std::move(name)), assignedToMission_(false)
{
}

const std::string &Submarine::GetSerialNumber() const { return serialNumber_; }
const std::string &Submarine::GetName() const { return name_; }
bool Submarine::IsAssignedToMission() const { return assignedToMission_; }

void Submarine::EndMission() { assignedToMission_ = false; }

void Submarine::SetAssigned(bool assigned) { assignedToMission_ = assigned; }
