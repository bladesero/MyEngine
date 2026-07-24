#pragma once

#include "API/RuntimeApi.h"

#include "Project/FormatVersions.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

struct UserDisplaySettings {
    int width = 1280;
    int height = 720;
    std::string windowMode = "windowed";
    bool vsync = true;
    int frameRateLimit = 60;
    bool pauseWhenUnfocused = true;
};

struct UserGraphicsSettings {
    std::string backend = "d3d11";
    int qualityLevel = 2;
    int shadowQuality = 2;
    bool postProcessing = true;
};

struct UserAudioSettings {
    float master = 1.0f;
    float music = 1.0f;
    float effects = 1.0f;
    float voice = 1.0f;
};

struct UserInputSettings {
    float mouseSensitivity = 1.0f;
    bool invertY = false;
    float gamepadDeadZone = 0.15f;
    float gamepadSensitivity = 1.0f;
    float vibration = 1.0f;
    nlohmann::json actionMap = nullptr;
};

struct UserAccessibilitySettings {
    bool subtitles = true;
    float subtitleScale = 1.0f;
    float uiScale = 1.0f;
    bool reduceCameraShake = false;
    bool highContrast = false;
    std::string colorVisionMode = "none";
};

struct RuntimeUserSettings {
    static constexpr int CurrentVersion = FormatVersions::UserSettings;
    int version = CurrentVersion;
    UserDisplaySettings display;
    UserGraphicsSettings graphics;
    UserAudioSettings audio;
    UserInputSettings input;
    UserAccessibilitySettings accessibility;
};

class MYENGINE_RUNTIME_API RuntimeUserSettingsStore {
public:
    static RuntimeUserSettings Defaults();
    static bool Validate(const RuntimeUserSettings& settings, std::string* error = nullptr);
    static nlohmann::json ToJson(const RuntimeUserSettings& settings);
    static bool FromJson(nlohmann::json value, RuntimeUserSettings& settings, std::string* error = nullptr);
    static bool Load(RuntimeUserSettings& settings, bool* usedBackup = nullptr, std::string* error = nullptr);
    static bool Save(const RuntimeUserSettings& settings, std::string* error = nullptr);
    static void SetStorageRoot(std::filesystem::path root);
    static void ClearStorageRootOverride();
    static std::filesystem::path GetStorageRoot();
    static std::filesystem::path GetSettingsPath();
};
