#include "Assets/MaterialAsset.h"

#include "Assets/AssetManager.h"
#include "Core/Logger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

const char* BlendModeToString(BlendMode mode)
{
    switch (mode) {
        case BlendMode::Opaque: return "Opaque";
        case BlendMode::AlphaTest: return "AlphaTest";
        case BlendMode::Transparent: return "Transparent";
        default: return "Opaque";
    }
}

BlendMode BlendModeFromString(const std::string& value)
{
    if (value == "AlphaTest") return BlendMode::AlphaTest;
    if (value == "Transparent") return BlendMode::Transparent;
    return BlendMode::Opaque;
}

const char* ParamTypeToString(MaterialParam::Type type)
{
    switch (type) {
        case MaterialParam::Type::Float: return "Float";
        case MaterialParam::Type::Vec2: return "Vec2";
        case MaterialParam::Type::Vec3: return "Vec3";
        case MaterialParam::Type::Vec4: return "Vec4";
        default: return "Float";
    }
}

MaterialParam::Type ParamTypeFromString(const std::string& value)
{
    if (value == "Vec2") return MaterialParam::Type::Vec2;
    if (value == "Vec3") return MaterialParam::Type::Vec3;
    if (value == "Vec4") return MaterialParam::Type::Vec4;
    return MaterialParam::Type::Float;
}

int ParamComponentCount(MaterialParam::Type type)
{
    switch (type) {
        case MaterialParam::Type::Float: return 1;
        case MaterialParam::Type::Vec2: return 2;
        case MaterialParam::Type::Vec3: return 3;
        case MaterialParam::Type::Vec4: return 4;
        default: return 1;
    }
}

std::filesystem::path ResolveTexturePath(
    const std::filesystem::path& materialPath,
    const std::string& texturePath)
{
    namespace fs = std::filesystem;
    fs::path path(texturePath);
    if (path.is_absolute() || texturePath.rfind("__builtin__/", 0) == 0 ||
        texturePath.rfind("__builtin__\\", 0) == 0) {
        return path;
    }
    return (materialPath.parent_path() / path).lexically_normal();
}

std::string MakeSavedTexturePath(
    const std::filesystem::path& materialPath,
    const std::string& texturePath)
{
    namespace fs = std::filesystem;
    if (texturePath.rfind("__builtin__/", 0) == 0 ||
        texturePath.rfind("__builtin__\\", 0) == 0) {
        return texturePath;
    }

    std::error_code ec;
    fs::path absoluteTexture = fs::absolute(fs::path(texturePath), ec);
    if (ec) return texturePath;

    const fs::path base = fs::absolute(materialPath.parent_path(), ec);
    if (ec) return texturePath;

    fs::path relative = fs::relative(absoluteTexture, base, ec);
    if (!ec && !relative.empty()) {
        return relative.generic_string();
    }
    return absoluteTexture.lexically_normal().string();
}

TextureHandle ResolveSceneTextureReference(const std::string& texturePath)
{
    auto& assets = AssetManager::Get();
    if (texturePath == "__builtin__/White") return assets.GetWhiteTexture();
    if (texturePath == "__builtin__/Black") return assets.GetBlackTexture();
    if (texturePath == "__builtin__/FlatNormal") return assets.GetNormalTexture();
    if (texturePath == "__builtin__/Checker") {
        TextureHandle cached = assets.GetByPath<TextureAsset>(texturePath);
        if (cached.IsValid()) return cached;

        constexpr int kTexSize = 16;
        constexpr int kCellSize = 2;
        std::vector<uint8_t> pixels(kTexSize * kTexSize * 4);
        for (int y = 0; y < kTexSize; ++y) {
            for (int x = 0; x < kTexSize; ++x) {
                const bool light = ((x / kCellSize) + (y / kCellSize)) % 2 == 0;
                const int idx = (y * kTexSize + x) * 4;
                pixels[idx + 0] = light ? 230 : 50;
                pixels[idx + 1] = light ? 130 : 50;
                pixels[idx + 2] = light ? 30 : 50;
                pixels[idx + 3] = 255;
            }
        }

        auto checkerTex = std::make_shared<TextureAsset>(texturePath);
        checkerTex->SetName("Checker");
        TextureDesc desc;
        desc.width = kTexSize;
        desc.height = kTexSize;
        desc.sRGB = false;
        checkerTex->SetPixelData(std::move(pixels), desc);
        return assets.Register(std::move(checkerTex));
    }

    const std::string resolved = assets.ResolvePath(texturePath);
    TextureHandle texture = assets.GetByPath<TextureAsset>(resolved);
    if (!texture.IsValid()) {
        texture = assets.Load<TextureAsset>(resolved);
    }
    return texture;
}

ShaderAssetHandle ResolveShaderReference(const std::string& shaderPath)
{
    if (shaderPath.empty()) return {};
    return AssetManager::Get().Load<ShaderAsset>(shaderPath);
}

} // namespace

std::shared_ptr<MaterialAsset> LoadMaterialAssetFromFile(const std::string& path)
{
    namespace fs = std::filesystem;
    try {
        std::ifstream input(path);
        if (!input.is_open()) {
            Logger::Error("[AssetManager] Could not open material: ", path);
            return {};
        }

        nlohmann::json data;
        input >> data;

        auto material = std::make_shared<MaterialAsset>(path);
        material->SetName(data.value("name", fs::path(path).stem().string()));
        material->SetBlendMode(BlendModeFromString(data.value("blendMode", std::string("Opaque"))));
        material->SetTwoSided(data.value("twoSided", false));
        material->SetWireframe(data.value("wireframe", false));
        material->SetAlphaThreshold(data.value("alphaThreshold", 0.5f));
        material->SetShaderAsset(ResolveShaderReference(data.value("shader", std::string())));

        const auto params = data.find("params");
        if (params != data.end() && params->is_object()) {
            for (auto it = params->begin(); it != params->end(); ++it) {
                const nlohmann::json& src = it.value();
                MaterialParam param;
                param.type = ParamTypeFromString(src.value("type", std::string("Float")));
                if (src.contains("data") && src["data"].is_array()) {
                    const int count = std::min<int>(
                        ParamComponentCount(param.type),
                        static_cast<int>(src["data"].size()));
                    for (int i = 0; i < count; ++i) {
                        param.data[i] = src["data"][i].get<float>();
                    }
                }
                material->SetParam(it.key(), param);
            }
        }

        const auto textures = data.find("textures");
        if (textures != data.end() && textures->is_object()) {
            for (auto it = textures->begin(); it != textures->end(); ++it) {
                if (!it.value().is_string()) continue;
                const std::string texturePath = it.value().get<std::string>();
                if (texturePath.empty()) continue;

                const fs::path resolved = ResolveTexturePath(path, texturePath);
                TextureHandle texture = AssetManager::Get().GetByPath<TextureAsset>(resolved.string());
                if (!texture.IsValid()) {
                    texture = AssetManager::Get().Load<TextureAsset>(resolved.string());
                }
                if (texture.IsValid()) {
                    material->SetTexture(it.key(), texture);
                } else {
                    Logger::Warn("[Material] Failed to resolve texture slot '",
                                 it.key(), "': ", texturePath);
                }
            }
        }

        material->MarkReady();
        return material;
    } catch (const std::exception& e) {
        Logger::Error("[Material] Failed to load '", path, "': ", e.what());
        return {};
    }
}

bool SaveMaterialAssetToFile(const MaterialAsset& material, const std::string& path)
{
    namespace fs = std::filesystem;
    try {
        const fs::path materialPath(path);
        if (!materialPath.parent_path().empty()) {
            fs::create_directories(materialPath.parent_path());
        }

        nlohmann::json data;
        data["type"] = "Material";
        data["name"] = material.GetName();
        data["blendMode"] = BlendModeToString(material.GetBlendMode());
        data["twoSided"] = material.IsTwoSided();
        data["wireframe"] = material.IsWireframe();
        data["alphaThreshold"] = material.GetAlphaThreshold();
        if (material.GetShaderAsset().IsValid())
            data["shader"] = AssetManager::Get().MakeProjectRelativePath(
                material.GetShaderAsset()->GetPath());

        nlohmann::json params = nlohmann::json::object();
        for (const auto& [name, param] : material.GetParams()) {
            nlohmann::json item;
            item["type"] = ParamTypeToString(param.type);
            item["data"] = nlohmann::json::array();
            const int count = ParamComponentCount(param.type);
            for (int i = 0; i < count; ++i) {
                item["data"].push_back(param.data[i]);
            }
            params[name] = std::move(item);
        }
        data["params"] = std::move(params);

        nlohmann::json textures = nlohmann::json::object();
        for (const auto& [slot, texture] : material.GetTextures()) {
            if (!texture.IsValid()) continue;
            textures[slot] = MakeSavedTexturePath(materialPath, texture->GetPath());
        }
        data["textures"] = std::move(textures);

        std::ofstream output(path);
        if (!output.is_open()) {
            Logger::Error("[Material] Could not write material: ", path);
            return false;
        }
        output << data.dump(2);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("[Material] Failed to save '", path, "': ", e.what());
        return false;
    }
}

void SerializeMaterialAssetForScene(const MaterialAsset& material, nlohmann::json& data)
{
    data = nlohmann::json::object();
    data["name"] = material.GetName();
    data["blendMode"] = BlendModeToString(material.GetBlendMode());
    data["twoSided"] = material.IsTwoSided();
    data["wireframe"] = material.IsWireframe();
    data["alphaThreshold"] = material.GetAlphaThreshold();
    if (material.GetShaderAsset().IsValid())
        data["shader"] = AssetManager::Get().MakeProjectRelativePath(
            material.GetShaderAsset()->GetPath());

    nlohmann::json params = nlohmann::json::object();
    for (const auto& [name, param] : material.GetParams()) {
        nlohmann::json item;
        item["type"] = ParamTypeToString(param.type);
        item["data"] = nlohmann::json::array();
        const int count = ParamComponentCount(param.type);
        for (int i = 0; i < count; ++i) {
            item["data"].push_back(param.data[i]);
        }
        params[name] = std::move(item);
    }
    data["params"] = std::move(params);

    nlohmann::json textures = nlohmann::json::object();
    for (const auto& [slot, texture] : material.GetTextures()) {
        if (!texture.IsValid()) continue;
        textures[slot] = AssetManager::Get().MakeProjectRelativePath(texture->GetPath());
    }
    data["textures"] = std::move(textures);
}

std::shared_ptr<MaterialAsset> LoadMaterialAssetFromScene(
    const nlohmann::json& data, const std::string& path)
{
    try {
        auto material = std::make_shared<MaterialAsset>(path);
        material->SetName(data.value("name", material->GetName()));
        material->SetBlendMode(BlendModeFromString(data.value("blendMode", std::string("Opaque"))));
        material->SetTwoSided(data.value("twoSided", false));
        material->SetWireframe(data.value("wireframe", false));
        material->SetAlphaThreshold(data.value("alphaThreshold", 0.5f));
        material->SetShaderAsset(ResolveShaderReference(data.value("shader", std::string())));

        const auto params = data.find("params");
        if (params != data.end() && params->is_object()) {
            for (auto it = params->begin(); it != params->end(); ++it) {
                const nlohmann::json& src = it.value();
                MaterialParam param;
                param.type = ParamTypeFromString(src.value("type", std::string("Float")));
                if (src.contains("data") && src["data"].is_array()) {
                    const int count = std::min<int>(
                        ParamComponentCount(param.type),
                        static_cast<int>(src["data"].size()));
                    for (int i = 0; i < count; ++i) {
                        param.data[i] = src["data"][i].get<float>();
                    }
                }
                material->SetParam(it.key(), param);
            }
        }

        const auto textures = data.find("textures");
        if (textures != data.end() && textures->is_object()) {
            for (auto it = textures->begin(); it != textures->end(); ++it) {
                if (!it.value().is_string()) continue;
                const std::string texturePath = it.value().get<std::string>();
                if (texturePath.empty()) continue;
                TextureHandle texture = ResolveSceneTextureReference(texturePath);
                if (texture.IsValid()) {
                    material->SetTexture(it.key(), texture);
                }
            }
        }

        material->MarkReady();
        return material;
    } catch (const std::exception& e) {
        Logger::Error("[Material] Failed to load scene-embedded material '", path,
                      "': ", e.what());
        return {};
    }
}
