#pragma once

#include "Assets/MaterialAsset.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

struct ResolvedMaterial {
    ShaderAssetHandle shader;
    BlendMode blendMode = BlendMode::Opaque;
    bool twoSided = false;
    bool wireframe = false;
    float alphaThreshold = 0.5f;
    std::unordered_map<std::string, MaterialParam> properties;
    std::unordered_map<std::string, TextureHandle> textures;
    std::array<Vec4, 32> constants{};
    std::vector<std::string> parentChain;
    std::vector<std::string> diagnostics;
    uint64_t shaderVersion = 0;
    bool valid = false;

    bool IsErrorMaterial() const { return !valid; }
    const MaterialParam* FindProperty(const std::string& id) const;
    TextureHandle FindTexture(const std::string& id) const;
};

class GpuBindGroup;
class MaterialResourceCache;

// Runtime-owned material resolution. MaterialAsset remains authoring data only;
// GPU-facing layouts and inherited values are produced here for every pass.
class MaterialSystem {
public:
    static constexpr size_t kMaxParentDepth = 64;
    static constexpr size_t kConstantSlotCount = 32;

    ResolvedMaterial Resolve(const MaterialAsset& material) const;
    ResolvedMaterial Resolve(const MaterialHandle& material) const;

    static bool IsCompatible(const ShaderPropertyDesc& property, const MaterialParam& value);
    static MaterialParam DefaultValue(const ShaderPropertyDesc& property);
    static bool BindTextures(GpuBindGroup& bindings, MaterialResourceCache& cache, const ResolvedMaterial& material,
                             std::vector<std::string>* diagnostics = nullptr);
    static std::string MakeTextureBindingName(const std::string& propertyId);
    static std::string MakeSamplerBindingName(const std::string& propertyId);

private:
    bool BuildChain(const MaterialAsset& material, std::vector<const MaterialAsset*>& chain,
                    std::vector<MaterialHandle>& loadedParents, std::vector<std::string>& active,
                    std::vector<std::string>& diagnostics) const;
};
