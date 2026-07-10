#pragma once

#include <string>
#include <vector>

class EditorClipboardService {
public:
    enum class Kind {
        None,
        Actors,
        Asset
    };

    void StoreActors(std::string json);
    void StoreAssets(std::vector<std::string> paths);
    void Clear();

    bool HasActors() const;
    bool HasAssets() const;
    Kind GetKind() const { return m_Kind; }

    const std::string& GetActorJson() const { return m_ActorJson; }
    const std::vector<std::string>& GetAssetPaths() const { return m_AssetPaths; }

private:
    Kind m_Kind = Kind::None;
    std::string m_ActorJson;
    std::vector<std::string> m_AssetPaths;
};
