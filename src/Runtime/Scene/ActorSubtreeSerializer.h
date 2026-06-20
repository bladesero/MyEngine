#pragma once

#include "Assets/PrefabAsset.h"

class Actor;

class ActorSubtreeSerializer {
public:
    static bool Serialize(const Actor& root, std::vector<PrefabNode>& nodes,
                          std::string* error = nullptr);
};
