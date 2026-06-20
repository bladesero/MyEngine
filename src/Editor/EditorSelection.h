#pragma once

#include <cstdint>
#include <string>
#include "Scene/ActorHandle.h"

class Actor;
class Scene;

class EditorSelection {
public:
    void SelectActorID(uint64_t actorID);
    void SelectActorHandle(ActorHandle actor);
    void SelectAssetPath(std::string path);
    void Clear();
    void Validate(Scene& scene);

    Actor* ResolveActor(Scene& scene) const;
    const Actor* ResolveActor(const Scene& scene) const;
    uint64_t GetActorID() const { return m_ActorID; }
    ActorHandle GetActorHandle() const { return m_ActorHandle; }
    const std::string& GetAssetPath() const { return m_AssetPath; }
    bool HasActor() const { return m_ActorHandle.IsValid() || m_ActorID != 0; }
    bool HasAsset() const { return !m_AssetPath.empty(); }

private:
    uint64_t m_ActorID = 0;
    ActorHandle m_ActorHandle;
    std::string m_AssetPath;
};
