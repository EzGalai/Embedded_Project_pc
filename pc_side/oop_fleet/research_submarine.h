/**
 * @file research_submarine.h
 * @brief Research submarine: used for research missions, carrying a set of
 * researchers and a current research topic (spec "OOP Part").
 */
#ifndef RESEARCH_SUBMARINE_H
#define RESEARCH_SUBMARINE_H

#include "submarine.h"

#include <string>
#include <vector>

class ResearchSubmarine : public Submarine {
public:
    ResearchSubmarine(std::string serialNumber, std::string name);

    std::string GetTypeName() const override;
    void AssignMission() override;
    void UpdateMissionDetails() override;
    void EndMission() override;
    void Display() const override;

private:
    void AddResearchers();

    std::string researchTopic_;
    std::vector<std::string> researchers_;
};

#endif
