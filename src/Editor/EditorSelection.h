#pragma once

#include <cstdint>
#include <string>

class Actor;
class Scene;

class EditorSelection {
public:
    void SelectActorID(uint64_t actorID);
    void SelectAssetPath(std::string path);
    void Clear();
    void Validate(Scene& scene);

    Actor* ResolveActor(Scene& scene) const;
    const Actor* ResolveActor(const Scene& scene) const;
    uint64_t GetActorID() const { return m_ActorID; }
    const std::string& GetAssetPath() const { return m_AssetPath; }
    bool HasActor() const { return m_ActorID != 0; }
    bool HasAsset() const { return !m_AssetPath.empty(); }

private:
    uint64_t m_ActorID = 0;
    std::string m_AssetPath;
};
