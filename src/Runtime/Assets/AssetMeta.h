#pragma once

#include "Assets/Asset.h"

#include <string>

struct AssetMeta {
    std::string uuid;
    std::string sourcePath;
    uint32_t importerVersion = 1;

    AssetID GetAssetID() const { return MakeAssetID(uuid); }

    static AssetMeta LoadOrCreate(const std::string& sourcePath);
    static bool Save(const AssetMeta& meta);
    static std::string MetaPathFor(const std::string& sourcePath);
};
