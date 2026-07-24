#pragma once

#include "API/RuntimeApi.h"

#include "Scene/Scene.h"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

struct SceneLoadPlan {
    nlohmann::json root;
    std::vector<nlohmann::json> actors;
    std::vector<std::string> assetDependencies;
};

struct SceneInstantiationState {
    size_t nextActor = 0;
    std::unordered_map<uint64_t, ActorHandle> handles;
    bool initialized = false;
    bool relationshipsQueued = false;
};

// ==========================================================================
// SceneSerializer  –  Scene ↔ JSON 文件 序列化
//
// 格式（scene.json）：
// {
//   "name": "MyScene",
//   "nextID": 5,
//   "actors": [
//     {
//       "id": 1, "name": "Root", "active": true,
//       "parentID": 0,
//       "transform": {
//         "position": [0,0,0],
//         "rotation": [0,0,0],
//         "scale":    [1,1,1]
//       },
//       "components": []
//     },
//     ...
//   ]
// }
//
// Component 序列化：
//   每个组件写出 "type" 字符串（GetTypeName()）和一个 "data" 对象。
//   子类可重写 Serialize/Deserialize 提供具体字段；基类默认写空 data。
// ==========================================================================

class MYENGINE_RUNTIME_API SceneSerializer {
public:
    // 将 scene 保存为 JSON 到指定文件路径，失败返回 false
    static bool SaveToFile(const Scene& scene, const std::string& filepath);

    // 从 JSON 文件加载到 scene（覆盖现有内容），失败返回 false
    static bool LoadFromFile(Scene& scene, const std::string& filepath);

    // 序列化为 JSON 字符串（便于调试/网络传输）
    static std::string SaveToString(const Scene& scene);

    // 从 JSON 字符串反序列化
    static bool LoadFromString(Scene& scene, const std::string& json);
    static bool BuildLoadPlan(const std::string& json, SceneLoadPlan& plan, std::string* error = nullptr);
    static bool InstantiateLoadPlan(Scene& scene, const SceneLoadPlan& plan, SceneInstantiationState& state,
                                    size_t maxActors, bool& complete, std::string* error = nullptr);
    static bool InstantiateLoadPlanAdditive(Scene& scene, const SceneLoadPlan& plan, SceneInstantiationState& state,
                                            size_t maxActors, std::vector<ActorHandle>& createdRoots, bool& complete,
                                            std::string* error = nullptr);
};
