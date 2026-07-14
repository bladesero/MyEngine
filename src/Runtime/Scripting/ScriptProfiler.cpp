#include "Scripting/ScriptProfiler.h"

#include <algorithm>
#include <mutex>
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace {
std::mutex g_ScriptProfilerMutex;
std::unordered_map<std::string, ScriptProfilerRecord> g_ScriptProfilerRecords;

std::string MakeKey(const std::string& scriptClass, const std::string& callback) {
    return scriptClass + "::" + callback;
}
} // namespace

void ScriptProfiler::Record(const std::string& scriptClass, const std::string& callback, double milliseconds,
                            bool failed) {
    std::lock_guard<std::mutex> lock(g_ScriptProfilerMutex);
    ScriptProfilerRecord& record = g_ScriptProfilerRecords[MakeKey(scriptClass, callback)];
    ++record.callCount;
    if (failed)
        ++record.exceptionCount;
    record.totalMilliseconds += std::max(0.0, milliseconds);
    record.maxMilliseconds = std::max(record.maxMilliseconds, milliseconds);
}

std::string ScriptProfiler::GetStatsJson() {
    nlohmann::json rows = nlohmann::json::array();
    std::lock_guard<std::mutex> lock(g_ScriptProfilerMutex);
    for (const auto& [key, record] : g_ScriptProfilerRecords) {
        rows.push_back({{"callback", key},
                        {"calls", record.callCount},
                        {"exceptions", record.exceptionCount},
                        {"totalMs", record.totalMilliseconds},
                        {"maxMs", record.maxMilliseconds}});
    }
    return rows.dump();
}

void ScriptProfiler::Reset() {
    std::lock_guard<std::mutex> lock(g_ScriptProfilerMutex);
    g_ScriptProfilerRecords.clear();
}
