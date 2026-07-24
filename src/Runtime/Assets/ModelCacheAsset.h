#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Assets/ModelAsset.h"

#include <memory>
#include <string>

MYENGINE_RUNTIME_API std::shared_ptr<ModelAsset> LoadModelCacheAssetFromFile(const std::string& path);
MYENGINE_RUNTIME_API bool SaveModelCacheAssetToFile(const ModelAsset& model, const std::string& path);
