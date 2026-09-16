/**
 * @file main.cpp
 * @brief central_computer entry point: launches the Ground Station server
 * thread (comm_gs.h), then connects to lnc_bridge and runs the LNC
 * link/streaming loop, reconnecting on disconnect. See PROJECT_PLAN.md §5.1,
 * §5.3, §6 Phase 8, §7.
 */

#include "protocol.h"
#include "lnc_link_client.h"
#include "management_command.h"
#include "log.h"
#include "comm_gs.h"

#include <cstdio>
#include <ctime>
#include <thread>
#include <chrono>
#include <unistd.h>

int main()
{
    /* Phase 15: GS serving runs continuously on its own thread, concurrent
       with the LNC handling below — see CC_GsLink_Run's own comment and
       data_collection.cpp's mutex for why this is safe. */
    std::thread gsThread(CC_GsLink_Run, GS_TCP_PORT);
    gsThread.detach();

    bool firstConnection = true;

    /* Phase 15: reconnect loop — a dropped LNC link (lnc_bridge restarting,
       or the LNC itself rebooting) no longer ends the program. It retries
       connecting until it succeeds, then resumes normal operation. */
    for (;;) {
        int fd = CcCore_LncConnect();
        if (fd < 0) {
            fprintf(stderr, "central_computer: no lnc_bridge connection, retrying in 3s...\n");
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }
        printf("central_computer: connected to lnc_bridge on 127.0.0.1:%d\n", BRIDGE_TCP_PORT);

        /* --- Get/Set time round-trip, re-synced on EVERY (re)connection ---
           Not just the first: this is what actually fixes the gap found
           during Watchdog testing — if the LNC itself rebooted (watchdog-
           triggered or otherwise) while this process kept running, its
           clock reverts to the fake boot baseline and needs re-syncing,
           the same as a fresh first connection would. */
        uint32_t initialTime = 0;
        bool ok = CcCore_GetTime(fd, &initialTime);
        if (ok) printf("central_computer: initial LNC time = %u\n", (unsigned)initialTime);

        uint32_t newTime = (uint32_t)time(nullptr); /* sync the LNC's RTC to this machine's real time */
        ProtoStatus_t setStatus = PROTO_STATUS_INTERNAL_ERROR;
        if (ok) {
            ok = CcCore_SetRtc(fd, newTime, &setStatus) && (setStatus == PROTO_STATUS_SUCCESS);
            if (ok) printf("central_computer: SET_RTC_REQ acknowledged, status=SUCCESS\n");
        }

        uint32_t confirmedTime = 0;
        if (ok) {
            ok = CcCore_GetTime(fd, &confirmedTime);
            if (ok) printf("central_computer: LNC time after set = %u\n", (unsigned)confirmedTime);
        }

        /* Exact equality would be flaky now that newTime is a real,
           continuously-advancing clock value rather than an artificial
           +1000s jump — a second can genuinely tick over between the SET
           and this GET due to normal round-trip latency. */
        bool match = ok && (confirmedTime >= newTime) && (confirmedTime <= newTime + 2);
        printf("Phase 8 test: %s\n", match ? "PASSED" : "FAILED");

        if (firstConnection) {
            /* --- Phase 12/13 one-time demo tests — proven once at first
               connection, not repeated on every reconnect (see the
               option-1-for-now decision: these stay as startup self-tests,
               to be cleaned up properly, if wanted, in Phase 17). --- */
            ProtoStatus_t setConfigStatus = PROTO_STATUS_INTERNAL_ERROR;
            bool configOk = CcCore_SetBatteryWarningMin(fd, 1500, &setConfigStatus) && (setConfigStatus == PROTO_STATUS_SUCCESS);
            printf("Phase 12 test: SET_BATTERY_WARNING_MIN %s\n", configOk ? "PASSED" : "FAILED");

            uint32_t queryStart = (newTime > 60) ? (newTime - 60) : 0;
            uint32_t queryEnd = confirmedTime + 60;
            CcCore_GetMeasurements(fd, queryStart, queryEnd);

            uint32_t eventsQueryStart = (newTime > 10) ? (newTime - 10) : 0;
            uint32_t eventsQueryEnd = confirmedTime + 10;
            CcCore_GetEvents(fd, eventsQueryStart, eventsQueryEnd);

            firstConnection = false;
        }

        /* --- Streaming loop: print/store KEEP_ALIVE/EVENT_REPORT until the
           link drops, then fall through to the outer loop and reconnect. --- */
        for (;;) {
            uint8_t storage[256];
            uint8_t tag;
            const uint8_t *value;
            uint16_t valueLen;

            if (!CcCore_LncRecvMessage(fd, &tag, &value, &valueLen, storage, sizeof(storage))) {
                fprintf(stderr, "central_computer: lnc_bridge disconnected\n");
                break;
            }
            if (tag == PROTO_TAG_EVENT_REPORT) {
                CcCore_PrintEventReport(value, valueLen);
            } else {
                printf("central_computer: received unhandled tag 0x%02X\n", tag);
            }
        }

        close(fd);
        fprintf(stderr, "central_computer: attempting to reconnect...\n");
    }
}
