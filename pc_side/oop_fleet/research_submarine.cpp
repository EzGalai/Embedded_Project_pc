/*
 * research_submarine.cpp — see research_submarine.h.
 */

#include "research_submarine.h"

#include <iostream>

ResearchSubmarine::ResearchSubmarine(std::string serialNumber, std::string name)
    : Submarine(std::move(serialNumber), std::move(name))
{
}

std::string ResearchSubmarine::GetTypeName() const { return "Research"; }

void ResearchSubmarine::AssignMission()
{
    researchTopic_ = PromptLine("Enter research topic: ");
    AddResearchers();
    SetAssigned(true);
}

void ResearchSubmarine::UpdateMissionDetails()
{
    std::string topic = PromptLine("Enter new research topic (blank to keep \"" + researchTopic_ + "\"): ");
    if (!topic.empty()) researchTopic_ = topic;

    std::string answer = PromptLine("Add more researchers? (y/n): ");
    if (!answer.empty() && (answer[0] == 'y' || answer[0] == 'Y')) AddResearchers();
}

void ResearchSubmarine::EndMission()
{
    researchTopic_.clear();
    researchers_.clear();
    Submarine::EndMission();
}

void ResearchSubmarine::Display() const
{
    std::cout << "[Research] Serial: " << GetSerialNumber() << ", Name: " << GetName()
               << ", Assigned: " << (IsAssignedToMission() ? "yes" : "no") << "\n";
    if (!IsAssignedToMission()) return;

    std::cout << "  Research topic: " << researchTopic_ << "\n";
    std::cout << "  Researchers: ";
    if (researchers_.empty()) {
        std::cout << "(none)";
    } else {
        for (size_t i = 0; i < researchers_.size(); ++i) {
            std::cout << researchers_[i] << (i + 1 < researchers_.size() ? ", " : "");
        }
    }
    std::cout << "\n";
}

void ResearchSubmarine::AddResearchers()
{
    std::cout << "Enter researcher names, one per line, blank line to finish:\n";
    for (;;) {
        std::string line = PromptLine("");
        if (line.empty()) break;
        researchers_.push_back(line);
    }
}
