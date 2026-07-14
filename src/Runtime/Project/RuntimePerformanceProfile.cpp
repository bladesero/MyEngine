#include "RuntimePerformanceProfile.h"

#include "Project/JsonMigrationRegistry.h"

#include <cmath>
#include <nlohmann/json.hpp>

namespace {

void SetError(std::string* error, const std::string& message) {
    if (error)
        *error = message;
}

bool PositiveFinite(double value) {
    return std::isfinite(value) && value > 0.0;
}

} // namespace

bool RuntimePerformanceProfile::FromJson(nlohmann::json value, RuntimePerformanceProfile& out, std::string* error) {
    if (!value.is_object()) {
        SetError(error, "performance profile root must be an object");
        return false;
    }
    if (!value.contains("version"))
        value["version"] = 0;
    JsonMigrationRegistry migrations("runtime performance profile", CurrentVersion);
    migrations.Register(0, [](nlohmann::json& json, std::string*) {
        if (!json.contains("budget")) {
            nlohmann::json budget = nlohmann::json::object();
            for (const char* field : {"warmupSamples", "minimumSamples", "maxP95FrameMs", "maxP99FrameMs", "maxFrameMs",
                                      "maxP95GpuMs", "maxWorkingSetGrowthBytes", "maxDroppedFixedTicks"}) {
                if (json.contains(field))
                    budget[field] = json[field];
            }
            json["budget"] = std::move(budget);
        }
        return true;
    });
    migrations.Register(1, [](nlohmann::json& json, std::string*) {
        if (!json.contains("scenario"))
            json["scenario"] = "warm-gameplay";
        if (!json.contains("resourceBudgetScale"))
            json["resourceBudgetScale"] = 1.0;
        if (!json.contains("sceneReloadCount"))
            json["sceneReloadCount"] = 0;
        return true;
    });
    migrations.Register(2, [](nlohmann::json& json, std::string*) {
        if (!json.contains("maxInitialSceneReadyMs"))
            json["maxInitialSceneReadyMs"] = 10000.0;
        if (!json.contains("maxSceneReloadMs"))
            json["maxSceneReloadMs"] = 5000.0;
        return true;
    });
    if (!migrations.Migrate(value, error))
        return false;

    const nlohmann::json budgetJson = value.value("budget", nlohmann::json::object());
    if (!budgetJson.is_object()) {
        SetError(error, "performance profile budget must be an object");
        return false;
    }
    RuntimePerformanceProfile parsed;
    parsed.version = value.value("version", 0u);
    parsed.name = value.value("name", parsed.name);
    parsed.hardwareClass = value.value("hardwareClass", parsed.hardwareClass);
    parsed.scenario = value.value("scenario", parsed.scenario);
    parsed.resourceBudgetScale = value.value("resourceBudgetScale", parsed.resourceBudgetScale);
    parsed.sceneReloadCount = value.value("sceneReloadCount", parsed.sceneReloadCount);
    parsed.maxInitialSceneReadyMs = value.value("maxInitialSceneReadyMs", parsed.maxInitialSceneReadyMs);
    parsed.maxSceneReloadMs = value.value("maxSceneReloadMs", parsed.maxSceneReloadMs);
    parsed.budget.warmupSamples = budgetJson.value("warmupSamples", parsed.budget.warmupSamples);
    parsed.budget.minimumSamples = budgetJson.value("minimumSamples", parsed.budget.minimumSamples);
    parsed.budget.maxP95FrameMs = budgetJson.value("maxP95FrameMs", parsed.budget.maxP95FrameMs);
    parsed.budget.maxP99FrameMs = budgetJson.value("maxP99FrameMs", parsed.budget.maxP99FrameMs);
    parsed.budget.maxFrameMs = budgetJson.value("maxFrameMs", parsed.budget.maxFrameMs);
    parsed.budget.maxP95GpuMs = budgetJson.value("maxP95GpuMs", parsed.budget.maxP95GpuMs);
    parsed.budget.maxWorkingSetGrowthBytes =
        budgetJson.value("maxWorkingSetGrowthBytes", parsed.budget.maxWorkingSetGrowthBytes);
    parsed.budget.maxDroppedFixedTicks = budgetJson.value("maxDroppedFixedTicks", parsed.budget.maxDroppedFixedTicks);

    const bool validScenario = parsed.scenario == "cold-start" || parsed.scenario == "warm-gameplay" ||
                               parsed.scenario == "scene-transition" || parsed.scenario == "resource-stress";
    if (parsed.name.empty() || parsed.hardwareClass.empty() || !validScenario ||
        !PositiveFinite(parsed.resourceBudgetScale) || parsed.resourceBudgetScale > 1.0 ||
        !PositiveFinite(parsed.maxInitialSceneReadyMs) || !PositiveFinite(parsed.maxSceneReloadMs) ||
        parsed.sceneReloadCount > 1000 || parsed.budget.minimumSamples == 0 || parsed.budget.minimumSamples > 1000000 ||
        parsed.budget.warmupSamples > 1000000 || !PositiveFinite(parsed.budget.maxP95FrameMs) ||
        !PositiveFinite(parsed.budget.maxP99FrameMs) || !PositiveFinite(parsed.budget.maxFrameMs) ||
        !PositiveFinite(parsed.budget.maxP95GpuMs) || parsed.budget.maxP95FrameMs > parsed.budget.maxP99FrameMs ||
        parsed.budget.maxP99FrameMs > parsed.budget.maxFrameMs) {
        SetError(error, "performance profile contains invalid names, samples, or timing budgets");
        return false;
    }
    out = std::move(parsed);
    return true;
}

bool RuntimePerformanceProfile::FromText(const std::string& text, RuntimePerformanceProfile& out, std::string* error) {
    try {
        return FromJson(nlohmann::json::parse(text), out, error);
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return false;
    }
}

nlohmann::json RuntimePerformanceProfile::ToJson() const {
    return {{"version", CurrentVersion},
            {"name", name},
            {"hardwareClass", hardwareClass},
            {"scenario", scenario},
            {"resourceBudgetScale", resourceBudgetScale},
            {"sceneReloadCount", sceneReloadCount},
            {"maxInitialSceneReadyMs", maxInitialSceneReadyMs},
            {"maxSceneReloadMs", maxSceneReloadMs},
            {"budget",
             {{"warmupSamples", budget.warmupSamples},
              {"minimumSamples", budget.minimumSamples},
              {"maxP95FrameMs", budget.maxP95FrameMs},
              {"maxP99FrameMs", budget.maxP99FrameMs},
              {"maxFrameMs", budget.maxFrameMs},
              {"maxP95GpuMs", budget.maxP95GpuMs},
              {"maxWorkingSetGrowthBytes", budget.maxWorkingSetGrowthBytes},
              {"maxDroppedFixedTicks", budget.maxDroppedFixedTicks}}}};
}
