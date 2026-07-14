#include "Editor/EditorClipboardService.h"

#include <utility>

void EditorClipboardService::StoreActors(std::string json) {
    m_ActorJson = std::move(json);
    m_AssetPaths.clear();
    m_Kind = Kind::Actors;
}

void EditorClipboardService::StoreAssets(std::vector<std::string> paths) {
    m_ActorJson.clear();
    m_AssetPaths = std::move(paths);
    m_Kind = m_AssetPaths.empty() ? Kind::None : Kind::Asset;
}

void EditorClipboardService::Clear() {
    m_ActorJson.clear();
    m_AssetPaths.clear();
    m_Kind = Kind::None;
}

bool EditorClipboardService::HasActors() const {
    return m_Kind == Kind::Actors && !m_ActorJson.empty();
}

bool EditorClipboardService::HasAssets() const {
    return m_Kind == Kind::Asset && !m_AssetPaths.empty();
}
