#pragma once

#include "API/RuntimeApi.h"

#include "Core/RuntimePerformanceBudget.h"
#include "Project/FormatVersions.h"

#include <nlohmann/json_fwd.hpp>
#include <string>

struct MYENGINE_RUNTIME_API RuntimePerformanceProfile {
    static constexpr uint32_t CurrentVersion = FormatVersions::RuntimePerformanceProfile;

    uint32_t version = CurrentVersion;
    std::string name = "desktop-60fps";
    std::string hardwareClass = "desktop-discrete";
    std::string scenario = "warm-gameplay";
    double resourceBudgetScale = 1.0;
    uint32_t sceneReloadCount = 0;
    double maxInitialSceneReadyMs = 10000.0;
    double maxSceneReloadMs = 5000.0;
    RuntimePerformanceBudget budget;

    static bool FromJson(nlohmann::json value, RuntimePerformanceProfile& out, std::string* error = nullptr);
    static bool FromText(const std::string& text, RuntimePerformanceProfile& out, std::string* error = nullptr);
    nlohmann::json ToJson() const;
};
