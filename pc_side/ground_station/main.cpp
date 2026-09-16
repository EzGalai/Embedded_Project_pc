/**
 * @file main.cpp
 * @brief ground_station — Phase 9: connects to central_computer's GS-facing
 * link and performs the "trivial handshake" (PROJECT_PLAN.md §6 Phase 9).
 * The real Phase 14 queries (Gs_RequestLog/Gs_RequestEvents) live in
 * gs_queries.cpp/.h — this file owns only the connection and handshake.
 *
 * The handshake itself is deliberately outside the TLV protocol — no
 * protocol.h message exists for it, since it only proves the CC<->GS
 * Ethernet link works, independent of the LNC link/bridge process.
 */

#include "gs_queries.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <ctime>
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define CC_GS_PORT 9000
#define GS_HANDSHAKE_GREETING "GS_HELLO"
#define GS_HANDSHAKE_REPLY    "CC_HELLO_ACK"

/**
 * @brief Prompts and reads one line of text (may be empty).
 */
static std::string PromptLine(const std::string &prompt)
{
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    return line;
}

/**
 * @brief Formats a Unix timestamp as "YYYY-MM-DD HH:MM:SS" (local time) —
 * used only to show a default's actual value in a prompt, so leaving it
 * blank isn't a guess as to what you'll get.
 */
static std::string FormatDateTime(uint32_t timestamp)
{
    time_t t = (time_t)timestamp;
    struct tm tmVal;
    localtime_r(&t, &tmVal);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmVal);
    return std::string(buf);
}

/**
 * @brief Prompts for a date/time as "YYYY-MM-DD HH:MM:SS" (local time),
 * returning defaultValue if left blank, reprompting if it doesn't parse.
 */
static uint32_t PromptDateTimeOrDefault(const std::string &prompt, uint32_t defaultValue)
{
    for (;;) {
        std::string line = PromptLine(prompt + " [" + FormatDateTime(defaultValue) + "]: ");
        if (line.empty()) return defaultValue;

        struct tm tmVal = {};
        if (strptime(line.c_str(), "%Y-%m-%d %H:%M:%S", &tmVal) != nullptr) {
            tmVal.tm_isdst = -1; /* let mktime figure out DST, don't assume either way */
            time_t t = mktime(&tmVal);
            if (t != (time_t)-1) return (uint32_t)t;
        }
        std::cout << "  Please enter as YYYY-MM-DD HH:MM:SS, or leave blank for the default.\n";
    }
}

int main(int argc, char *argv[])
{
    std::string ccHost = "127.0.0.1";
    uint16_t ccPort = CC_GS_PORT;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--cc-host") == 0 && i + 1 < argc) {
            ccHost = argv[++i];
        } else if (strcmp(argv[i], "--cc-port") == 0 && i + 1 < argc) {
            ccPort = (uint16_t)std::atoi(argv[++i]);
        } else {
            fprintf(stderr, "ground_station: unrecognized argument '%s'\n", argv[i]);
            fprintf(stderr, "usage: %s [--cc-host host] [--cc-port port]\n", argv[0]);
            return 1;
        }
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "ground_station: socket() failed: %s\n", strerror(errno));
        return 1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ccPort);
    if (inet_pton(AF_INET, ccHost.c_str(), &addr.sin_addr) != 1) {
        fprintf(stderr, "ground_station: invalid --cc-host '%s'\n", ccHost.c_str());
        close(fd);
        return 1;
    }

    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "ground_station: connect() to central_computer failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("ground_station: connected to central_computer on %s:%u\n", ccHost.c_str(), ccPort);

    /* Send the length-prefixed greeting */
    uint16_t greetingLen = (uint16_t)strlen(GS_HANDSHAKE_GREETING);
    uint8_t prefix[4] = {0, 0, (uint8_t)(greetingLen >> 8), (uint8_t)greetingLen};
    if (send(fd, prefix, sizeof(prefix), 0) < 0 || send(fd, GS_HANDSHAKE_GREETING, greetingLen, 0) < 0) {
        fprintf(stderr, "ground_station: send() failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("ground_station: sent greeting: %s\n", GS_HANDSHAKE_GREETING);

    /* Block waiting for the length-prefixed reply */
    if (recv(fd, prefix, sizeof(prefix), MSG_WAITALL) != sizeof(prefix)) {
        fprintf(stderr, "ground_station: failed to read reply length prefix: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    uint32_t replyLen = ((uint32_t)prefix[0] << 24) | ((uint32_t)prefix[1] << 16) |
                         ((uint32_t)prefix[2] << 8)  | (uint32_t)prefix[3];

    char reply[64] = {0};
    if (replyLen >= sizeof(reply) || recv(fd, reply, replyLen, MSG_WAITALL) != (ssize_t)replyLen) {
        fprintf(stderr, "ground_station: failed to read reply\n");
        close(fd);
        return 1;
    }
    printf("ground_station: received reply: %s\n", reply);

    bool match = (strcmp(reply, GS_HANDSHAKE_REPLY) == 0);
    if (!match) {
        fprintf(stderr, "ground_station: handshake with central_computer failed (unexpected reply)\n");
    }

    if (match) {
        /* --- Phase 14: real GS queries ---
           Interactive range, as absolute date/time — matches the spec's own
           wording ("data stored over a period of time (date and time)")
           more literally than a relative "hours ago" scheme would, and uses
           the same "YYYY-MM-DD HH:MM:SS" (local time) format already used
           for display everywhere else (see gs_queries.cpp's FormatUnixTime).
           Left blank, defaults to 24h ago through now — DCA data is always
           dated by receipt time, so nothing in storage is ever timestamped
           in the future anyway. */
        std::string submarineId = PromptLine("Submarine ID (blank for LNC-01): ");
        if (submarineId.empty()) submarineId = "LNC-01";

        uint32_t nowTs = (uint32_t)time(nullptr);
        uint32_t defaultStart = (nowTs > 86400) ? nowTs - 86400 : 0;

        uint32_t rangeStart = PromptDateTimeOrDefault("Range start", defaultStart);
        uint32_t rangeEnd = PromptDateTimeOrDefault("Range end", nowTs);
        if (rangeStart > rangeEnd) std::swap(rangeStart, rangeEnd);

        Gs_RequestLog(fd, submarineId, rangeStart, rangeEnd);
        Gs_RequestEvents(fd, submarineId, rangeStart, rangeEnd);
    }

    close(fd);
    return match ? 0 : 1;
}
