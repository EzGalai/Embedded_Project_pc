/*
 * data_collection.cpp — see data_collection.h.
 */

#include "data_collection.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace fs = std::filesystem;

static const int DCA_MAX_FILES = 7;

/* Phase 15: central_computer now serves Ground Station queries on their own
   thread, concurrently with the main thread storing incoming LNC data — so
   every file operation here needs to be serialized, same reasoning as the
   LNC's own xLogMutexHandle in log.c/retrieval.c. */
static std::mutex g_dcaMutex;

/* Library-level default — untouched fallback for any caller that never
   invokes DCA_SetDataDir. Both central_computer's and oop_fleet's main()
   always do call it (with a CLI-overridable default of "../data", landing
   at the shared pc_side/data/ per §7), so this literal is what's actually
   in effect only for a hypothetical caller that skips that step. */
static std::string g_dataDir = "data";

void DCA_SetDataDir(const std::string &dir)
{
    g_dataDir = dir;
}

/**
 * @brief This machine's current date as "YYYY-MM-DD" (UTC), used to name
 * the file a write lands in — see data_collection.h's note on why receipt
 * time is used instead of the submarine's reported timestamp.
 */
static std::string TodayDateString()
{
    time_t t = time(nullptr);
    struct tm tmVal;
    gmtime_r(&t, &tmVal);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmVal.tm_year + 1900, tmVal.tm_mon + 1, tmVal.tm_mday);
    return std::string(buf);
}

/**
 * @brief Deletes the oldest file in dir if more than DCA_MAX_FILES exist.
 * Filenames are "YYYY-MM-DD.log", so lexicographic sort order is also
 * chronological order.
 */
static void RotateIfNeeded(const fs::path &dir)
{
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::vector<fs::path> files;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }
    if ((int)files.size() <= DCA_MAX_FILES) return;

    std::sort(files.begin(), files.end());
    fs::remove(files.front(), ec);
}

static void AppendLine(const std::string &submarineId, const char *category, const std::string &line)
{
    std::lock_guard<std::mutex> lock(g_dcaMutex);

    fs::path dir = fs::path(g_dataDir) / submarineId / category;
    RotateIfNeeded(dir);

    fs::path file = dir / (TodayDateString() + ".log");
    std::ofstream out(file, std::ios::app);
    out << line << "\n";
}

void DCA_StoreMeasurement(const std::string &submarineId, uint32_t timestamp, int16_t temperature,
                           uint8_t humidity, uint16_t light, uint16_t batteryVoltage, uint8_t mode)
{
    std::ostringstream oss;
    oss << timestamp << "," << temperature << "," << (unsigned)humidity << ","
        << light << "," << batteryVoltage << "," << (unsigned)mode;
    AppendLine(submarineId, "measurements", oss.str());
}

void DCA_StoreEvent(const std::string &submarineId, uint32_t timestamp, uint8_t eventType, uint8_t eventSource,
                     bool hasMeasurement, int16_t temperature, uint8_t humidity, uint16_t light,
                     uint16_t batteryVoltage, uint8_t mode)
{
    std::ostringstream oss;
    oss << timestamp << "," << (unsigned)eventType << "," << (unsigned)eventSource;
    if (hasMeasurement) {
        oss << "," << temperature << "," << (unsigned)humidity << ","
            << light << "," << batteryVoltage << "," << (unsigned)mode;
    }
    AppendLine(submarineId, "events", oss.str());
}

/**
 * @brief Reads every line from every regular file in dir, oldest filename
 * first. Missing directory just yields an empty result.
 */
static std::vector<std::string> ReadAllLines(const fs::path &dir)
{
    std::lock_guard<std::mutex> lock(g_dcaMutex);

    std::vector<std::string> lines;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return lines;

    std::vector<fs::path> files;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto &f : files) {
        std::ifstream in(f);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) lines.push_back(line);
        }
    }
    return lines;
}

std::vector<DcaMeasurement> DCA_QueryMeasurements(const std::string &submarineId, uint32_t start, uint32_t end)
{
    std::vector<DcaMeasurement> result;
    fs::path dir = fs::path(g_dataDir) / submarineId / "measurements";

    for (const auto &line : ReadAllLines(dir)) {
        DcaMeasurement m;
        int temperature;
        unsigned humidity, light, battery, mode;
        if (sscanf(line.c_str(), "%u,%d,%u,%u,%u,%u", &m.timestamp, &temperature, &humidity, &light, &battery, &mode) != 6) {
            continue; /* malformed/truncated line — skip it */
        }
        if (m.timestamp < start || m.timestamp > end) continue;

        m.temperature = (int16_t)temperature;
        m.humidity = (uint8_t)humidity;
        m.light = (uint16_t)light;
        m.batteryVoltage = (uint16_t)battery;
        m.mode = (uint8_t)mode;
        result.push_back(m);
    }
    return result;
}

std::vector<DcaEvent> DCA_QueryEvents(const std::string &submarineId, uint32_t start, uint32_t end)
{
    std::vector<DcaEvent> result;
    fs::path dir = fs::path(g_dataDir) / submarineId / "events";

    for (const auto &line : ReadAllLines(dir)) {
        uint32_t ts;
        unsigned eventType, eventSource;
        int temperature;
        unsigned humidity, light, battery, mode;
        int scanned = sscanf(line.c_str(), "%u,%u,%u,%d,%u,%u,%u,%u",
                              &ts, &eventType, &eventSource, &temperature, &humidity, &light, &battery, &mode);
        bool hasMeasurement = (scanned == 8);
        if (!hasMeasurement && scanned != 3) continue; /* malformed/truncated line — skip it */
        if (ts < start || ts > end) continue;

        DcaEvent e;
        e.timestamp = ts;
        e.eventType = (uint8_t)eventType;
        e.eventSource = (uint8_t)eventSource;
        e.hasMeasurement = hasMeasurement;
        if (hasMeasurement) {
            e.measurement.timestamp = ts;
            e.measurement.temperature = (int16_t)temperature;
            e.measurement.humidity = (uint8_t)humidity;
            e.measurement.light = (uint16_t)light;
            e.measurement.batteryVoltage = (uint16_t)battery;
            e.measurement.mode = (uint8_t)mode;
        }
        result.push_back(e);
    }
    return result;
}
