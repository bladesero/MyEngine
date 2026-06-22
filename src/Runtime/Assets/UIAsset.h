#pragma once

#include "Assets/Asset.h"

#include <memory>
#include <string>
#include <utility>

class UIPrefabAsset final : public Asset {
public:
    explicit UIPrefabAsset(const std::string& path)
        : Asset(AssetType::UIPrefab, path) {}

    void SetDocumentPath(std::string path) { m_DocumentPath = std::move(path); SetState(AssetState::Ready); }
    const std::string& GetDocumentPath() const { return m_DocumentPath; }

private:
    std::string m_DocumentPath;
};

class UIStyleAsset final : public Asset {
public:
    explicit UIStyleAsset(const std::string& path)
        : Asset(AssetType::UIStyle, path) { SetState(AssetState::Ready); }
};

class UIFontAsset final : public Asset {
public:
    explicit UIFontAsset(const std::string& path)
        : Asset(AssetType::UIFont, path) { SetState(AssetState::Ready); }
};

using UIPrefabHandle = AssetHandle<UIPrefabAsset>;
using UIStyleHandle = AssetHandle<UIStyleAsset>;
using UIFontHandle = AssetHandle<UIFontAsset>;
