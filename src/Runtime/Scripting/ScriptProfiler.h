#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include <cstdint>
#include <string>

struct ScriptProfilerRecord {
    uint64_t callCount = 0;
    uint64_t exceptionCount = 0;
    double totalMilliseconds = 0.0;
    double maxMilliseconds = 0.0;
};

class MYENGINE_RUNTIME_API ScriptProfiler {
public:
    static void Record(const std::string& scriptClass, const std::string& callback, double milliseconds, bool failed);
    static std::string GetStatsJson();
    static void Reset();
};
