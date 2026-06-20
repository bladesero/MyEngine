#pragma once

#include "Assets/Asset.h"

#include <string>
#include <optional>

struct AssetMeta {
    std::string uuid;
    std::string sourcePath;
    uint32_t importerVersion = 1;

    AssetID GetAssetID() const { return MakeAssetID(uuid); }

    static std::optional<AssetMeta> Load(const std::string& sourcePath,
                                         std::string* error = nullptr);
    static AssetMeta Create(const std::string& sourcePath);
    static AssetMeta LoadOrCreate(const std::string& sourcePath);
    static bool Save(const AssetMeta& meta);
    static std::string MetaPathFor(const std::string& sourcePath);
};
