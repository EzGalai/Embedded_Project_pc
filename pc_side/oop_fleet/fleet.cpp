/*
 * fleet.cpp — see fleet.h.
 */

#include "fleet.h"
#include "research_submarine.h"
#include "combat_submarine.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iostream>

void Fleet::AddSubmarine()
{
    std::string serial = PromptLine("Enter serial number: ");
    if (FindBySerial(serial) != nullptr) {
        std::cout << "A submarine with that serial number already exists.\n";
        return;
    }
    std::string name = PromptLine("Enter name: ");

    int type = PromptInt("Type (1=Research, 2=Combat): ");
    if (type == 1) {
        submarines_.push_back(std::make_unique<ResearchSubmarine>(serial, name));
    } else if (type == 2) {
        submarines_.push_back(std::make_unique<CombatSubmarine>(serial, name));
    } else {
        std::cout << "Unknown type, submarine not added.\n";
        return;
    }
    std::cout << "Added.\n";
}

void Fleet::DisplayAll() const
{
    if (submarines_.empty()) {
        std::cout << "Fleet is empty.\n";
        return;
    }
    for (const auto &sub : submarines_) {
        sub->Display();
    }
}

void Fleet::SearchAndDisplay() const
{
    std::string serial = PromptLine("Enter serial number: ");
    Submarine *sub = FindBySerial(serial);
    if (sub == nullptr) {
        std::cout << "No submarine with that serial number.\n";
        return;
    }
    sub->Display();
}

void Fleet::AssignMission()
{
    Submarine *sub = FindBySerial(PromptLine("Enter serial number: "));
    if (sub == nullptr) {
        std::cout << "No submarine with that serial number.\n";
        return;
    }
    if (sub->IsAssignedToMission()) {
        std::cout << "That submarine is already assigned to a mission — end it first.\n";
        return;
    }
    sub->AssignMission();
    std::cout << "Mission assigned.\n";
}

void Fleet::UpdateMissionDetails()
{
    Submarine *sub = FindBySerial(PromptLine("Enter serial number: "));
    if (sub == nullptr) {
        std::cout << "No submarine with that serial number.\n";
        return;
    }
    if (!sub->IsAssignedToMission()) {
        std::cout << "That submarine isn't assigned to a mission.\n";
        return;
    }
    sub->UpdateMissionDetails();
    std::cout << "Updated.\n";
}

void Fleet::EndMission()
{
    Submarine *sub = FindBySerial(PromptLine("Enter serial number: "));
    if (sub == nullptr) {
        std::cout << "No submarine with that serial number.\n";
        return;
    }
    if (!sub->IsAssignedToMission()) {
        std::cout << "That submarine isn't assigned to a mission.\n";
        return;
    }

    if (auto *combat = dynamic_cast<CombatSubmarine *>(sub)) {
        for (const auto &otherSerial : combat->GetParticipatingSubmarines()) {
            if (auto *other = dynamic_cast<CombatSubmarine *>(FindBySerial(otherSerial))) {
                other->RemoveParticipant(combat->GetSerialNumber());
            }
        }
    }
    sub->EndMission();
    std::cout << "Mission ended.\n";
}

void Fleet::AssociateCombatSubmarines()
{
    auto *a = dynamic_cast<CombatSubmarine *>(FindBySerial(PromptLine("Enter this submarine's serial number: ")));
    if (a == nullptr) {
        std::cout << "No combat submarine with that serial number.\n";
        return;
    }
    if (!a->IsAssignedToMission()) {
        std::cout << "That submarine isn't assigned to a mission.\n";
        return;
    }

    auto *b = dynamic_cast<CombatSubmarine *>(FindBySerial(PromptLine("Enter the other combat submarine's serial number: ")));
    if (b == nullptr) {
        std::cout << "No combat submarine with that serial number.\n";
        return;
    }
    if (!b->IsAssignedToMission()) {
        std::cout << "That submarine isn't assigned to a mission.\n";
        return;
    }

    a->AddParticipant(b->GetSerialNumber());
    b->AddParticipant(a->GetSerialNumber());
    std::cout << "Associated.\n";
}

void Fleet::SendMessage()
{
    auto *sender = dynamic_cast<CombatSubmarine *>(FindBySerial(PromptLine("Enter sender's serial number: ")));
    if (sender == nullptr) {
        std::cout << "No combat submarine with that serial number.\n";
        return;
    }

    std::string receiverSerial = PromptLine("Enter receiver's serial number: ");
    auto *receiver = dynamic_cast<CombatSubmarine *>(FindBySerial(receiverSerial));
    if (receiver == nullptr) {
        std::cout << "No combat submarine with that serial number.\n";
        return;
    }

    if (!sender->HasParticipant(receiverSerial)) {
        std::cout << "Those two submarines aren't participating in the same mission.\n";
        return;
    }

    std::string content = PromptLine("Enter message: ");
    receiver->ReceiveMessage(sender->GetSerialNumber(), content);
    std::cout << "Message sent.\n";
}

void Fleet::DisplayReceivedMessages() const
{
    auto *sub = dynamic_cast<CombatSubmarine *>(FindBySerial(PromptLine("Enter serial number: ")));
    if (sub == nullptr) {
        std::cout << "No combat submarine with that serial number.\n";
        return;
    }

    const auto &messages = sub->GetReceivedMessages();
    if (messages.empty()) {
        std::cout << "No messages received.\n";
        return;
    }
    for (const auto &m : messages) {
        std::cout << "From " << m.senderSerialNumber << ": " << m.content << "\n";
    }
}

void Fleet::ViewHistoricalData()
{
    auto *sub = dynamic_cast<CombatSubmarine *>(FindBySerial(PromptLine("Enter serial number: ")));
    if (sub == nullptr) {
        std::cout << "No combat submarine with that serial number.\n";
        return;
    }
    if (!sub->GetCentralComputer().IsConnected()) {
        std::cout << "That submarine's central computer isn't connected to an LNC.\n";
        return;
    }

    int hoursBackStart = PromptInt("How many hours back should the range start? ");
    int hoursBackEnd = PromptInt("How many hours back should the range end (0 = now)? ");

    uint32_t now = (uint32_t)time(nullptr);
    uint32_t startAgo = (uint32_t)hoursBackStart * 3600;
    uint32_t endAgo = (uint32_t)hoursBackEnd * 3600;
    uint32_t start = (startAgo > now) ? 0 : now - startAgo;
    uint32_t end = (endAgo > now) ? 0 : now - endAgo;
    if (start > end) std::swap(start, end);

    std::cout << "-- Measurements --\n";
    sub->GetCentralComputer().QueryMeasurements(start, end);
    std::cout << "-- Events --\n";
    sub->GetCentralComputer().QueryEvents(start, end);
}

Submarine *Fleet::FindBySerial(const std::string &serial) const
{
    for (const auto &sub : submarines_) {
        if (sub->GetSerialNumber() == serial) return sub.get();
    }
    return nullptr;
}
