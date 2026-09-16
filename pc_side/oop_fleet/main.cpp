/**
 * @file main.cpp
 * @brief OOP Fleet Management System entry point (spec "OOP Part",
 * PROJECT_PLAN.md §4.19/§7/Phase 16): a menu loop dispatching to Fleet's
 * 10 operations.
 */

#include "submarine.h"
#include "fleet.h"
#include "data_collection.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

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

/* Baked in at build time (by the Makefile/CMakeLists.txt) as an absolute
   path to the shared pc_side/data/ (§7) — deliberately not a runtime-
   relative default, since the "correct" relative path to it differs
   between the Makefile-built binary (run from within oop_fleet/) and the
   CMake-built one (run from pc_side/, per §8's example commands). */
#ifndef DEFAULT_DATA_DIR
#define DEFAULT_DATA_DIR "../data"
#endif

int main(int argc, char *argv[])
{
    std::string dataDir = DEFAULT_DATA_DIR;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            dataDir = argv[++i];
        } else {
            fprintf(stderr, "oop_fleet: unrecognized argument '%s'\n", argv[i]);
            fprintf(stderr, "usage: %s [--data-dir path]\n", argv[0]);
            return 1;
        }
    }
    DCA_SetDataDir(dataDir);

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
