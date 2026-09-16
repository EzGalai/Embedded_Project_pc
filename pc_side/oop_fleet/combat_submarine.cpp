/*
 * combat_submarine.cpp — see combat_submarine.h.
 */

#include "combat_submarine.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iostream>

CombatSubmarine::CombatSubmarine(std::string serialNumber, std::string name)
    : Submarine(std::move(serialNumber), std::move(name)), personnelCount_(0)
{
}

std::string CombatSubmarine::GetTypeName() const { return "Combat"; }

void CombatSubmarine::AssignMission()
{
    missionDescription_ = PromptLine("Enter mission description: ");
    commanderName_ = PromptLine("Enter commander name: ");
    personnelCount_ = PromptInt("Enter number of combat personnel: ");
    SetAssigned(true);

    std::string host = PromptLine("Enter this submarine's lnc_bridge host (blank to skip live LNC connection): ");
    if (!host.empty()) {
        int port = PromptInt("Enter lnc_bridge port: ");
        if (centralComputer_.Connect(GetSerialNumber(), host, (uint16_t)port)) {
            std::cout << "  Connected to LNC.\n";
        } else {
            std::cout << "  Could not connect to LNC — proceeding without live telemetry.\n";
        }
    }
}

void CombatSubmarine::UpdateMissionDetails()
{
    std::string value = PromptLine("Enter new mission description (blank to keep \"" + missionDescription_ + "\"): ");
    if (!value.empty()) missionDescription_ = value;

    value = PromptLine("Enter new commander name (blank to keep \"" + commanderName_ + "\"): ");
    if (!value.empty()) commanderName_ = value;

    value = PromptLine("Enter new personnel count (blank to keep " + std::to_string(personnelCount_) + "): ");
    if (!value.empty()) {
        try {
            personnelCount_ = std::stoi(value);
        } catch (...) {
            std::cout << "  Not a number, personnel count left unchanged.\n";
        }
    }
}

void CombatSubmarine::EndMission()
{
    if (!missionDescription_.empty()) missionHistory_.push_back(missionDescription_);
    missionDescription_.clear();
    commanderName_.clear();
    personnelCount_ = 0;
    participatingSubmarines_.clear();
    centralComputer_.Disconnect();
    Submarine::EndMission();
}

void CombatSubmarine::Display() const
{
    std::cout << "[Combat] Serial: " << GetSerialNumber() << ", Name: " << GetName()
               << ", Assigned: " << (IsAssignedToMission() ? "yes" : "no") << "\n";
    if (IsAssignedToMission()) {
        std::cout << "  Mission: " << missionDescription_ << "\n";
        std::cout << "  Commander: " << commanderName_ << ", Personnel: " << personnelCount_ << "\n";
        std::cout << "  Participating submarines: ";
        if (participatingSubmarines_.empty()) {
            std::cout << "(none)";
        } else {
            for (size_t i = 0; i < participatingSubmarines_.size(); ++i) {
                std::cout << participatingSubmarines_[i] << (i + 1 < participatingSubmarines_.size() ? ", " : "");
            }
        }
        std::cout << "\n";
    }
    if (!missionHistory_.empty()) {
        std::cout << "  Mission history: ";
        for (size_t i = 0; i < missionHistory_.size(); ++i) {
            std::cout << missionHistory_[i] << (i + 1 < missionHistory_.size() ? "; " : "");
        }
        std::cout << "\n";
    }

    if (centralComputer_.IsConnected()) {
        centralComputer_.PollLatestTelemetry();
    }
    if (centralComputer_.HasLatestTelemetry()) {
        char timeStr[32];
        time_t t = (time_t)centralComputer_.GetLatestTimestamp();
        struct tm tmVal;
        localtime_r(&t, &tmVal);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tmVal);
        std::cout << "  Central computer [" << (centralComputer_.IsConnected() ? "connected" : "disconnected")
                   << "] last telemetry [" << timeStr << "]: temp=" << (centralComputer_.GetLatestTemperature() / 10.0)
                   << "C humidity=" << (unsigned)centralComputer_.GetLatestHumidity()
                   << "% light=" << (unsigned)(centralComputer_.GetLatestLight() * 100 / 4095)
                   << "% battery=" << (unsigned)(centralComputer_.GetLatestBattery() * 100 / 3300)
                   << "% mode=" << (unsigned)centralComputer_.GetLatestMode() << "\n";
    } else if (centralComputer_.IsConnected()) {
        std::cout << "  Central computer: connected, no telemetry received yet.\n";
    }
}

CentralComputer &CombatSubmarine::GetCentralComputer() { return centralComputer_; }

void CombatSubmarine::AddParticipant(const std::string &otherSerial)
{
    if (otherSerial == GetSerialNumber()) return;
    if (!HasParticipant(otherSerial)) {
        participatingSubmarines_.push_back(otherSerial);
    }
}

void CombatSubmarine::RemoveParticipant(const std::string &otherSerial)
{
    participatingSubmarines_.erase(
        std::remove(participatingSubmarines_.begin(), participatingSubmarines_.end(), otherSerial),
        participatingSubmarines_.end());
}

bool CombatSubmarine::HasParticipant(const std::string &otherSerial) const
{
    return std::find(participatingSubmarines_.begin(), participatingSubmarines_.end(), otherSerial)
           != participatingSubmarines_.end();
}

void CombatSubmarine::ReceiveMessage(const std::string &senderSerial, const std::string &content)
{
    receivedMessages_.push_back(Message{content, senderSerial});
}

const std::vector<Message> &CombatSubmarine::GetReceivedMessages() const { return receivedMessages_; }
const std::vector<std::string> &CombatSubmarine::GetParticipatingSubmarines() const { return participatingSubmarines_; }
