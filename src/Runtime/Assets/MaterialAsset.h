#pragma once

#include "Assets/Asset.h"
#include "Assets/TextureAsset.h"
#include "Assets/ShaderAsset.h"
#include "Core/EngineMath.h"
#include "Renderer/IRenderContext.h"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

// ==========================================================================
// MaterialAsset  –  材质资产
//
// 存储：
//   - 着色器（GpuShader 句柄）
//   - 纹理槽（按名字绑定 TextureAsset）
//   - 标量/向量参数（uniform 值）
//   - 渲染状态标志（双面/透明/线框等）
// ==========================================================================

// --------------------------------------------------------------------------
// 材质渲染模式
// --------------------------------------------------------------------------
enum class BlendMode : uint8_t {
    Opaque = 0,
    AlphaTest,   // discard below threshold
    Transparent, // alpha blending
};

// --------------------------------------------------------------------------
// 材质参数（标量 / 向量）
// --------------------------------------------------------------------------
struct MaterialParam {
    enum class Type { Float, Vec2, Vec3, Vec4 } type = Type::Float;
    float data[4] = {0.f, 0.f, 0.f, 1.f};

    static MaterialParam FromFloat(float v) {
        MaterialParam p;
        p.type = Type::Float;
        p.data[0] = v;
        return p;
    }
    static MaterialParam FromVec2(float x, float y) {
        MaterialParam p;
        p.type = Type::Vec2;
        p.data[0] = x;
        p.data[1] = y;
        return p;
    }
    static MaterialParam FromVec3(float x, float y, float z) {
        MaterialParam p;
        p.type = Type::Vec3;
        p.data[0] = x;
        p.data[1] = y;
        p.data[2] = z;
        return p;
    }
    static MaterialParam FromVec4(float x, float y, float z, float w) {
        MaterialParam p;
        p.type = Type::Vec4;
        p.data[0] = x;
        p.data[1] = y;
        p.data[2] = z;
        p.data[3] = w;
        return p;
    }
    static MaterialParam FromColor(const Vec3& c, float a = 1.f) { return FromVec4(c.x, c.y, c.z, a); }
};

// --------------------------------------------------------------------------
// MaterialAsset
// --------------------------------------------------------------------------
class MaterialAsset : public Asset {
public:
    static constexpr uint32_t kVersion = 2;
    enum SurfaceOverride : uint8_t {
        OverrideBlendMode = 1u << 0u,
        OverrideTwoSided = 1u << 1u,
        OverrideWireframe = 1u << 2u,
        OverrideAlphaThreshold = 1u << 3u,
        OverrideAllSurface = OverrideBlendMode | OverrideTwoSided | OverrideWireframe | OverrideAlphaThreshold,
    };

    explicit MaterialAsset(const std::string& path) : Asset(AssetType::Material, path) {}

    uint32_t GetFormatVersion() const { return m_FormatVersion; }
    bool WasLoadedFromLegacyFormat() const { return m_LoadedFromLegacyFormat; }
    const std::string& GetParentPath() const { return m_ParentPath; }
    void SetParentPath(std::string path) { m_ParentPath = std::move(path); }
    bool HasParent() const { return !m_ParentPath.empty(); }
    uint8_t GetSurfaceOverrideMask() const { return m_SurfaceOverrideMask; }
    void SetSurfaceOverrideMask(uint8_t value) { m_SurfaceOverrideMask = value; }
    bool OverridesSurface(SurfaceOverride field) const { return (m_SurfaceOverrideMask & field) != 0; }

    // ---- 基础参数 ----------------------------------------------------------
    BlendMode GetBlendMode() const { return m_BlendMode; }
    bool IsTwoSided() const { return m_TwoSided; }
    bool IsWireframe() const { return m_Wireframe; }
    float GetAlphaThreshold() const { return m_AlphaThreshold; }

    void SetBlendMode(BlendMode bm) {
        m_BlendMode = bm;
        m_SurfaceOverrideMask |= OverrideBlendMode;
    }
    void SetTwoSided(bool v) {
        m_TwoSided = v;
        m_SurfaceOverrideMask |= OverrideTwoSided;
    }
    void SetWireframe(bool v) {
        m_Wireframe = v;
        m_SurfaceOverrideMask |= OverrideWireframe;
    }
    void SetAlphaThreshold(float v) {
        m_AlphaThreshold = v;
        m_SurfaceOverrideMask |= OverrideAlphaThreshold;
    }

    // ---- 纹理槽 ------------------------------------------------------------
    void SetTexture(const std::string& slot, TextureHandle tex) { m_Textures[slot] = std::move(tex); }
    TextureHandle GetTexture(const std::string& slot) const {
        auto it = m_Textures.find(slot);
        return (it != m_Textures.end()) ? it->second : TextureHandle{};
    }
    bool HasTexture(const std::string& slot) const { return m_Textures.count(slot) > 0; }
    void RemoveTexture(const std::string& slot) { m_Textures.erase(slot); }

    const std::unordered_map<std::string, TextureHandle>& GetTextures() const { return m_Textures; }

    // ---- 标量/向量参数 -----------------------------------------------------
    void SetParam(const std::string& name, const MaterialParam& p) { m_Params[name] = p; }
    MaterialParam GetParam(const std::string& name, const MaterialParam& def = {}) const {
        auto it = m_Params.find(name);
        return (it != m_Params.end()) ? it->second : def;
    }
    bool HasParam(const std::string& name) const { return m_Params.count(name) > 0; }
    void RemoveParam(const std::string& name) { m_Params.erase(name); }

    float GetFloat(const std::string& n, float def = 0.f) const {
        auto it = m_Params.find(n);
        return it != m_Params.end() ? it->second.data[0] : def;
    }
    Vec3 GetColor(const std::string& n, Vec3 def = Vec3::One()) const {
        auto it = m_Params.find(n);
        if (it == m_Params.end())
            return def;
        return {it->second.data[0], it->second.data[1], it->second.data[2]};
    }

    const std::unordered_map<std::string, MaterialParam>& GetParams() const { return m_Params; }

    // ---- GPU 着色器句柄（由渲染后端填写）----------------------------------
    void SetShader(std::shared_ptr<GpuShader> shader) {
        m_Shader = std::move(shader);
        SetState(AssetState::Ready);
    }
    GpuShader* GetShader() const { return m_Shader.get(); }
    const std::shared_ptr<GpuShader>& GetShaderHandle() const { return m_Shader; }
    bool HasShader() const { return m_Shader != nullptr; }
    void SetShaderAsset(ShaderAssetHandle shader) { m_ShaderAsset = std::move(shader); }
    const ShaderAssetHandle& GetShaderAsset() const { return m_ShaderAsset; }
    void MarkReady() { SetState(AssetState::Ready); }

    bool ReloadFrom(const Asset& source) override {
        const auto* material = dynamic_cast<const MaterialAsset*>(&source);
        if (!material)
            return false;
        m_BlendMode = material->m_BlendMode;
        m_TwoSided = material->m_TwoSided;
        m_Wireframe = material->m_Wireframe;
        m_AlphaThreshold = material->m_AlphaThreshold;
        m_FormatVersion = material->m_FormatVersion;
        m_LoadedFromLegacyFormat = material->m_LoadedFromLegacyFormat;
        m_ParentPath = material->m_ParentPath;
        m_SurfaceOverrideMask = material->m_SurfaceOverrideMask;
        m_PreservedFields = material->m_PreservedFields;
        m_Textures = material->m_Textures;
        m_Params = material->m_Params;
        m_ShaderAsset = material->m_ShaderAsset;
        m_Shader = material->m_Shader;
        SetState(AssetState::Ready);
        return true;
    }

    // ---- 工厂：常用预设材质 -----------------------------------------------
    static std::shared_ptr<MaterialAsset> CreateDefaultAtPath(const std::string& path, const std::string& name) {
        auto mat = std::make_shared<MaterialAsset>(path);
        mat->SetName(name);
        mat->SetParam("BaseColor", MaterialParam::FromColor({1, 1, 1}));
        mat->SetParam("Metallic", MaterialParam::FromFloat(0.f));
        mat->SetParam("Roughness", MaterialParam::FromFloat(0.5f));
        mat->SetState(AssetState::Ready);
        return mat;
    }
    static std::shared_ptr<MaterialAsset> CreateDefault(const std::string& name = "Default") {
        return CreateDefaultAtPath("__builtin__/" + name, name);
    }

private:
    friend std::shared_ptr<MaterialAsset> LoadMaterialAssetFromFile(const std::string& path);
    friend bool SaveMaterialAssetToFile(const MaterialAsset& material, const std::string& path);

    uint32_t m_FormatVersion = kVersion;
    bool m_LoadedFromLegacyFormat = false;
    std::string m_ParentPath;
    uint8_t m_SurfaceOverrideMask = OverrideAllSurface;
    nlohmann::json m_PreservedFields = nlohmann::json::object();
    BlendMode m_BlendMode = BlendMode::Opaque;
    bool m_TwoSided = false;
    bool m_Wireframe = false;
    float m_AlphaThreshold = 0.5f;

    std::unordered_map<std::string, TextureHandle> m_Textures;
    std::unordered_map<std::string, MaterialParam> m_Params;
    std::shared_ptr<GpuShader> m_Shader;
    ShaderAssetHandle m_ShaderAsset;
};

using MaterialHandle = AssetHandle<MaterialAsset>;

std::shared_ptr<MaterialAsset> LoadMaterialAssetFromFile(const std::string& path);
bool SaveMaterialAssetToFile(const MaterialAsset& material, const std::string& path);
void SerializeMaterialAssetForScene(const MaterialAsset& material, nlohmann::json& data);
std::shared_ptr<MaterialAsset> LoadMaterialAssetFromScene(const nlohmann::json& data, const std::string& path);
