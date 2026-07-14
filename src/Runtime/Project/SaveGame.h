#pragma once
#include "Project/FormatVersions.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <vector>

struct SaveGameMetadata {
    std::string displayName;
    std::string scene;
    std::string savedAtUtc;
    double playTimeSeconds = 0.0;
    std::string screenshot;
    std::string buildId;
};

struct SaveGameData {
    static constexpr int CurrentVersion=FormatVersions::SaveGame;
    int version=CurrentVersion;
    SaveGameMetadata metadata;
    std::string checkpoint;
    nlohmann::json player=nlohmann::json::object();
    std::vector<std::string> collected;
    nlohmann::json settings=nlohmann::json::object();
};

struct SaveGameSlotInfo {
    std::string slot;
    SaveGameMetadata metadata;
    bool valid = false;
    bool hasBackup = false;
    std::string error;
};

class SaveGame {
public:
    static bool Write(const std::string& slot,const SaveGameData& data,std::string* error=nullptr);
    static bool Read(const std::string& slot,SaveGameData& data,std::string* error=nullptr);
    static bool ReadBackup(const std::string& slot,SaveGameData& data,std::string* error=nullptr);
    static bool RestoreBackup(const std::string& slot,std::string* error=nullptr);
    static std::vector<SaveGameSlotInfo> ListSlots();
    static bool WriteAutosave(const SaveGameData& data,size_t slotCount=3,
                              std::string* writtenSlot=nullptr,std::string* error=nullptr);
    static bool WriteCheckpoint(const std::string& checkpointId,const SaveGameData& data,
                                std::string* writtenSlot=nullptr,std::string* error=nullptr);
    static bool FindLatestAutosave(SaveGameSlotInfo& info);
    static bool Exists(const std::string& slot);static bool Remove(const std::string& slot,std::string* error=nullptr);
    static void SetStorageRoot(std::filesystem::path root);
    static void ClearStorageRootOverride();
    static std::filesystem::path GetStorageRoot();
    static nlohmann::json ToJson(const SaveGameData& data);static bool FromJson(nlohmann::json value,SaveGameData& data,std::string* error=nullptr);
private:static bool Resolve(const std::string& slot,std::string& path,std::string* error);
};
