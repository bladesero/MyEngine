#pragma once

#include "API/RuntimeApi.h"

#include "Assets/PrefabAsset.h"

#include <unordered_map>
#include <unordered_set>

class Actor;

class MYENGINE_RUNTIME_API ActorSubtreeSerializer {
public:
    static bool Serialize(const Actor& root, std::vector<PrefabNode>& nodes, std::string* error = nullptr,
                          const std::unordered_set<const Actor*>* excludedRoots = nullptr,
                          std::unordered_map<const Actor*, std::string>* actorLocalIds = nullptr);
};
