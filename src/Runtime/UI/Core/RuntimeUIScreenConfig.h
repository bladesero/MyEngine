#pragma once

#include "Project/FormatVersions.h"

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

class RuntimeUIScreenStack;

struct RuntimeUIScreenOverride {
    std::string stableName;
    std::string title;
    std::string documentPath;
    std::unordered_map<std::string, std::string> actionLabels;
};

class RuntimeUIScreenConfig {
public:
    static constexpr uint32_t CurrentVersion = FormatVersions::RuntimeUIScreenConfig;
    static constexpr const char* DefaultPath = "Content/Config/RuntimeScreens.ui.json";
    uint32_t version = CurrentVersion;
    std::vector<RuntimeUIScreenOverride> screens;

    static bool FromJson(const nlohmann::json&, RuntimeUIScreenConfig&, std::string* error = nullptr);
    static bool FromText(const std::string&, RuntimeUIScreenConfig&, std::string* error = nullptr);
    bool Apply(RuntimeUIScreenStack&, std::string* error = nullptr) const;
};
