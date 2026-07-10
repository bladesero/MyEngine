#pragma once

#include "Scene/Scene.h"

#include <filesystem>
#include <string>
#include <vector>

struct PrefabNode {
    std::string localId;
    std::string parentLocalId;
    std::string name;
    bool activeSelf = true;
    uint32_t editorFlags = 0;
    Transform transform;
    std::vector<ComponentCreateDesc> components;
};

struct PrefabAsset {
    static constexpr uint32_t kVersion = 1;
    uint32_t version = kVersion;
    std::string uuid;
    std::string rootLocalId;
    std::vector<PrefabNode> nodes;

    static bool Load(const std::filesystem::path& path, PrefabAsset& result,
                     std::string* error = nullptr);
    bool Save(const std::filesystem::path& path, std::string* error = nullptr) const;
    bool Validate(std::string* error = nullptr) const;
};

nlohmann::json PrefabNodeToJson(const PrefabNode& node);
bool PrefabNodeFromJson(const nlohmann::json& json, PrefabNode& node,
                        std::string* error = nullptr);
