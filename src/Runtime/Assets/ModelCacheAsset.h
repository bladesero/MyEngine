#pragma once

#include "Assets/ModelAsset.h"

#include <memory>
#include <string>

std::shared_ptr<ModelAsset> LoadModelCacheAssetFromFile(const std::string& path);
bool SaveModelCacheAssetToFile(const ModelAsset& model, const std::string& path);

