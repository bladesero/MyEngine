#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Assets/SceneData.h"
#include "Project/FormatVersions.h"

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

struct PrefabNestedInstance {
    std::string instanceLocalId;
    std::string parentLocalId;
    std::string assetPath;
    std::string assetUuid;
    uint64_t sourceRevision = 1;
    Transform rootTransform;
    nlohmann::json overrides = nlohmann::json::array();
};

struct MYENGINE_RUNTIME_API PrefabAsset {
    static constexpr uint32_t kVersion = FormatVersions::Prefab;
    uint32_t version = kVersion;
    std::string uuid;
    uint64_t revision = 1;
    std::string rootLocalId;
    std::vector<PrefabNode> nodes;
    std::vector<PrefabNestedInstance> nestedInstances;

    static bool Load(const std::filesystem::path& path, PrefabAsset& result, std::string* error = nullptr);
    bool Save(const std::filesystem::path& path, std::string* error = nullptr) const;
    bool Validate(std::string* error = nullptr) const;
};

MYENGINE_RUNTIME_API nlohmann::json PrefabNodeToJson(const PrefabNode& node);
MYENGINE_RUNTIME_API bool PrefabNodeFromJson(const nlohmann::json& json, PrefabNode& node, std::string* error = nullptr);
