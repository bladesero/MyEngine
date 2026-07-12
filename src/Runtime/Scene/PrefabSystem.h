#pragma once

#include "Assets/PrefabAsset.h"
#include "Scene/ActorHandle.h"

#include <filesystem>
#include <optional>

class Actor;
class Scene;

struct PrefabInstantiateOptions {
    ActorHandle parent;
    std::optional<Transform> rootTransform;
    uint64_t persistentRootID = 0;
    std::string expectedUuid;
    nlohmann::json overrides = nlohmann::json::array();
    std::string nestedInstanceLocalId;
};

class PrefabSystem {
public:
    static bool SaveSubtree(const Actor& root, const std::filesystem::path& path,
                            std::string* error = nullptr);
    static ActorHandle QueueInstantiate(Scene& scene, const std::filesystem::path& path,
                                        const PrefabInstantiateOptions& options = {},
                                        std::string* error = nullptr);
    static Actor* Instantiate(Scene& scene, const std::filesystem::path& path,
                              const PrefabInstantiateOptions& options = {},
                              std::string* error = nullptr);

    static bool CaptureOverrides(Actor& instanceRoot, std::string* error = nullptr);
    static bool BuildOverrides(const Actor& instanceRoot, nlohmann::json& overrides,
                               std::string* error = nullptr);
    static bool SetInstanceOverrides(Actor& instanceRoot, nlohmann::json overrides,
                                     std::string* error = nullptr);
    static bool ApplyOverridesToAsset(PrefabAsset& asset,
                                      const nlohmann::json& overrides,
                                      std::string* error = nullptr);
    static bool ApplyAll(Actor& instanceRoot, std::string* error = nullptr);
    static bool RevertAll(Actor& instanceRoot, std::string* error = nullptr);
    static bool Unpack(Actor& instanceRoot, std::string* error = nullptr);
    static bool RefreshInstances(Scene& scene, const std::string& prefabUuid,
                                 std::string* error = nullptr);

    static std::filesystem::path ResolvePrefabPath(const std::filesystem::path& path);
};
