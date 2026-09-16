/**
 * @file main.cpp
 * @brief OOP Fleet Management System entry point (spec "OOP Part",
 * PROJECT_PLAN.md §4.19/§7/Phase 16): a menu loop dispatching to Fleet's
 * 10 operations.
 */

#include "submarine.h"
#include "fleet.h"

#include <iostream>

static void PrintMenu()
{
    std::cout << "\n--- Fleet Management ---\n"
                 "1. Add a new submarine\n"
                 "2. Display all submarines\n"
                 "3. Search for a submarine by serial number\n"
                 "4. Assign a mission to a submarine\n"
                 "5. Update a submarine's mission details\n"
                 "6. End a submarine's mission\n"
                 "7. Associate combat submarines with the same mission\n"
                 "8. Send a message between combat submarines\n"
                 "9. Display messages received by a submarine\n"
                 "10. Exit\n"
                 "11. View a combat submarine's historical LNC data (extra)\n"
                 "Choice: ";
}

int main()
{
    Fleet fleet;

    for (;;) {
        PrintMenu();
        int choice = PromptInt("");

        switch (choice) {
            case 1: fleet.AddSubmarine(); break;
            case 2: fleet.DisplayAll(); break;
            case 3: fleet.SearchAndDisplay(); break;
            case 4: fleet.AssignMission(); break;
            case 5: fleet.UpdateMissionDetails(); break;
            case 6: fleet.EndMission(); break;
            case 7: fleet.AssociateCombatSubmarines(); break;
            case 8: fleet.SendMessage(); break;
            case 9: fleet.DisplayReceivedMessages(); break;
            case 10:
                std::cout << "Exiting.\n";
                return 0;
            case 11: fleet.ViewHistoricalData(); break;
            default:
                std::cout << "Unknown choice.\n";
                break;
        }
    }
}
