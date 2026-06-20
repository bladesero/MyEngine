#pragma once

#include <string>

struct AssetRef {
    std::string uuid;
    std::string subAsset;
    std::string fallbackPath;

    bool IsValid() const { return !uuid.empty() || !fallbackPath.empty(); }
    bool operator==(const AssetRef& other) const {
        return uuid == other.uuid && subAsset == other.subAsset &&
               fallbackPath == other.fallbackPath;
    }
};
