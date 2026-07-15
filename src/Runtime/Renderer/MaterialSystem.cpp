#include "Renderer/MaterialSystem.h"

#include "Assets/AssetManager.h"
#include "Renderer/MaterialResourceCache.h"
#include "Renderer/RHI/GpuBindGroup.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace {

std::string CanonicalMaterialPath(const std::string& path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    if (!ec)
        return canonical.generic_string();
    return std::filesystem::path(path).lexically_normal().generic_string();
}

TextureHandle ResolveDefaultTexture(const std::string& path) {
    auto& assets = AssetManager::Get();
    if (path.empty() || path == "__builtin__/White")
        return assets.GetWhiteTexture();
    if (path == "__builtin__/Black")
        return assets.GetBlackTexture();
    if (path == "__builtin__/FlatNormal")
        return assets.GetNormalTexture();
    return assets.Load<TextureAsset>(assets.ResolvePath(path));
}

} // namespace

std::string MaterialSystem::MakeTextureBindingName(const std::string& propertyId) {
    std::string name = propertyId;
    for (char& c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            c = '_';
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front())))
        name.insert(name.begin(), '_');
    return "g_Texture_" + name;
}

std::string MaterialSystem::MakeSamplerBindingName(const std::string& propertyId) {
    std::string textureName = MakeTextureBindingName(propertyId);
    return "g_Sampler_" + textureName.substr(std::string("g_Texture_").size());
}

bool MaterialSystem::BindTextures(GpuBindGroup& bindings, MaterialResourceCache& cache,
                                  const ResolvedMaterial& material, std::vector<std::string>* diagnostics) {
    if (!material.shader.IsValid())
        return false;
    bool succeeded = true;
    for (const ShaderPropertyDesc& property : material.shader->GetProperties()) {
        if (property.type != ShaderPropertyType::Texture2D)
            continue;
        TextureHandle textureHandle = material.FindTexture(property.id);
        TextureAsset* texture = textureHandle.Get();
        if (texture)
            cache.EnsureTextureUploaded(texture);
        GpuTexture* gpuTexture = texture ? static_cast<GpuTexture*>(texture->GetGpuHandle()) : nullptr;
        const bool textureSet =
            bindings.SetTexture(MakeTextureBindingName(property.id), cache.GetTextureView(gpuTexture));
        const bool samplerSet =
            bindings.SetSampler(MakeSamplerBindingName(property.id),
                                texture ? cache.GetSamplerForTexture(texture) : cache.GetLinearSampler());
        if (!textureSet || !samplerSet) {
            succeeded = false;
            if (diagnostics)
                diagnostics->push_back("Failed to bind texture property '" + property.id + "'.");
        }
    }
    return succeeded;
}

const MaterialParam* ResolvedMaterial::FindProperty(const std::string& id) const {
    const auto it = properties.find(id);
    return it == properties.end() ? nullptr : &it->second;
}

TextureHandle ResolvedMaterial::FindTexture(const std::string& id) const {
    const auto it = textures.find(id);
    return it == textures.end() ? TextureHandle{} : it->second;
}

MaterialParam MaterialSystem::DefaultValue(const ShaderPropertyDesc& property) {
    switch (property.type) {
    case ShaderPropertyType::Float:
    case ShaderPropertyType::Bool:
        return MaterialParam::FromFloat(property.defaultValue[0]);
    case ShaderPropertyType::Vec2:
        return MaterialParam::FromVec2(property.defaultValue[0], property.defaultValue[1]);
    case ShaderPropertyType::Vec3:
        return MaterialParam::FromVec3(property.defaultValue[0], property.defaultValue[1], property.defaultValue[2]);
    case ShaderPropertyType::Color:
        return MaterialParam::FromVec4(property.defaultValue[0], property.defaultValue[1], property.defaultValue[2],
                                       property.defaultValue[3]);
    case ShaderPropertyType::Texture2D:
        return {};
    }
    return {};
}

bool MaterialSystem::IsCompatible(const ShaderPropertyDesc& property, const MaterialParam& value) {
    switch (property.type) {
    case ShaderPropertyType::Float:
    case ShaderPropertyType::Bool:
        return value.type == MaterialParam::Type::Float;
    case ShaderPropertyType::Vec2:
        return value.type == MaterialParam::Type::Vec2;
    case ShaderPropertyType::Vec3:
        return value.type == MaterialParam::Type::Vec3;
    case ShaderPropertyType::Color:
        return value.type == MaterialParam::Type::Vec4;
    case ShaderPropertyType::Texture2D:
        return false;
    }
    return false;
}

bool MaterialSystem::BuildChain(const MaterialAsset& material, std::vector<const MaterialAsset*>& chain,
                                std::vector<MaterialHandle>& loadedParents, std::vector<std::string>& active,
                                std::vector<std::string>& diagnostics) const {
    const std::string key = CanonicalMaterialPath(material.GetPath());
    if (std::find(active.begin(), active.end(), key) != active.end()) {
        diagnostics.push_back("Material parent cycle detected at '" + material.GetPath() + "'.");
        return false;
    }
    if (active.size() >= kMaxParentDepth) {
        diagnostics.push_back("Material parent chain exceeds 64 assets.");
        return false;
    }

    active.push_back(key);
    if (material.HasParent()) {
        auto& assets = AssetManager::Get();
        const std::string parentPath = assets.ResolvePath(material.GetParentPath());
        MaterialHandle parent = assets.GetByPath<MaterialAsset>(parentPath);
        if (!parent.IsValid())
            parent = assets.Load<MaterialAsset>(parentPath);
        if (!parent.IsValid()) {
            diagnostics.push_back("Missing material parent '" + material.GetParentPath() + "'.");
            active.pop_back();
            return false;
        }
        loadedParents.push_back(parent);
        if (!BuildChain(*parent, chain, loadedParents, active, diagnostics)) {
            active.pop_back();
            return false;
        }
    }
    chain.push_back(&material);
    active.pop_back();
    return true;
}

ResolvedMaterial MaterialSystem::Resolve(const MaterialHandle& material) const {
    if (!material.IsValid()) {
        ResolvedMaterial result;
        result.diagnostics.push_back("Material handle is missing.");
        return result;
    }
    return Resolve(*material);
}

ResolvedMaterial MaterialSystem::Resolve(const MaterialAsset& material) const {
    ResolvedMaterial result;
    std::vector<const MaterialAsset*> chain;
    std::vector<MaterialHandle> loadedParents;
    std::vector<std::string> active;
    if (!BuildChain(material, chain, loadedParents, active, result.diagnostics))
        return result;

    for (const MaterialAsset* item : chain) {
        result.parentChain.push_back(item->GetPath());
        if (item->GetShaderAsset().IsValid())
            result.shader = item->GetShaderAsset();
    }
    if (!result.shader.IsValid()) {
        result.diagnostics.push_back("Material has no shader after resolving its parent chain.");
        return result;
    }
    if (result.shader->GetDomain() == ShaderDomain::Compute) {
        result.diagnostics.push_back("A compute shader cannot be used by a material.");
        return result;
    }

    result.shaderVersion = result.shader->GetSourceHash();
    switch (result.shader->GetSurfaceType()) {
    case ShaderSurfaceType::Masked:
        result.blendMode = BlendMode::AlphaTest;
        break;
    case ShaderSurfaceType::Transparent:
        result.blendMode = BlendMode::Transparent;
        break;
    default:
        result.blendMode = BlendMode::Opaque;
        break;
    }
    for (const ShaderPropertyDesc& property : result.shader->GetProperties()) {
        if (property.type == ShaderPropertyType::Texture2D) {
            TextureHandle texture = ResolveDefaultTexture(property.defaultTexture);
            if (texture.IsValid())
                result.textures[property.id] = texture;
        } else {
            result.properties[property.id] = DefaultValue(property);
        }
    }

    for (const MaterialAsset* item : chain) {
        if (item->OverridesSurface(MaterialAsset::OverrideBlendMode))
            result.blendMode = item->GetBlendMode();
        if (item->OverridesSurface(MaterialAsset::OverrideTwoSided))
            result.twoSided = item->IsTwoSided();
        if (item->OverridesSurface(MaterialAsset::OverrideWireframe))
            result.wireframe = item->IsWireframe();
        if (item->OverridesSurface(MaterialAsset::OverrideAlphaThreshold))
            result.alphaThreshold = item->GetAlphaThreshold();

        for (const auto& [key, value] : item->GetParams()) {
            const ShaderPropertyDesc* property = result.shader->FindPropertyById(key);
            if (!property && item->WasLoadedFromLegacyFormat())
                property = result.shader->FindPropertyByName(key);
            if (!property) {
                result.diagnostics.push_back("Unknown material property override '" + key + "'.");
                continue;
            }
            if (property->type == ShaderPropertyType::Texture2D || !IsCompatible(*property, value)) {
                result.diagnostics.push_back("Type mismatch for material property '" + property->id + "'.");
                continue;
            }
            result.properties[property->id] = value;
        }
        for (const auto& [key, texture] : item->GetTextures()) {
            const ShaderPropertyDesc* property = result.shader->FindPropertyById(key);
            if (!property && item->WasLoadedFromLegacyFormat())
                property = result.shader->FindPropertyByName(key);
            if (!property) {
                result.diagnostics.push_back("Unknown material texture override '" + key + "'.");
                continue;
            }
            if (property->type != ShaderPropertyType::Texture2D) {
                result.diagnostics.push_back("Texture assigned to non-texture property '" + property->id + "'.");
                continue;
            }
            if (texture.IsValid())
                result.textures[property->id] = texture;
        }
    }

    if (result.shader->GetDomain() == ShaderDomain::Surface) {
        const bool forwardSurface =
            result.blendMode == BlendMode::Transparent || result.shader->GetShadingModel() == ShaderShadingModel::Unlit;
        const ShaderPass colorPass = forwardSurface ? ShaderPass::Forward : ShaderPass::GBuffer;
        if (!result.shader->HasPass(colorPass)) {
            result.diagnostics.push_back(std::string("Resolved surface requires missing ") + ShaderPassName(colorPass) +
                                         " pass.");
            return result;
        }
        if (result.blendMode != BlendMode::Transparent && !result.shader->HasPass(ShaderPass::Shadow)) {
            result.diagnostics.push_back("Resolved opaque/masked surface requires missing Shadow pass.");
            return result;
        }
    }

    for (const ShaderPropertyDesc& property : result.shader->GetProperties()) {
        if (property.type == ShaderPropertyType::Texture2D) {
            if (!result.FindTexture(property.id).IsValid()) {
                result.diagnostics.push_back("Texture property '" + property.id +
                                             "' has no usable default or override.");
                return result;
            }
            continue;
        }
        if (property.constantSlot >= result.constants.size()) {
            result.diagnostics.push_back("Property '" + property.id + "' exceeds the material constant layout.");
            return result;
        }
        const MaterialParam* value = result.FindProperty(property.id);
        if (!value)
            continue;
        result.constants[property.constantSlot] = {value->data[0], value->data[1], value->data[2], value->data[3]};
    }

    result.valid = true;
    return result;
}
