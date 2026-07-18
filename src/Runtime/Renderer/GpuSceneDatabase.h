#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Core/EngineMath.h"
#include "Renderer/RHI/IRHIDevice.h"
#include "Renderer/MaterialResourceCache.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

class Actor;
class MeshAsset;
class Scene;

struct GpuGeometryAllocation {
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};

class GpuGeometryArena {
public:
    explicit GpuGeometryArena(IRHIDevice* device = nullptr) : m_Device(device) {}

    void SetDevice(IRHIDevice* device) { m_Device = device; }
    bool EnsureMeshes(const std::vector<MeshAsset*>& meshes, uint64_t frameNumber);
    void CollectRetired(uint64_t frameNumber);
    const GpuGeometryAllocation* Find(const MeshAsset* mesh) const;
    const std::shared_ptr<GpuBuffer>& GetVertexBuffer() const { return m_VertexBuffer; }
    const std::shared_ptr<GpuBuffer>& GetIndexBuffer() const { return m_IndexBuffer; }
    uint64_t GetLastUploadBytes() const { return m_LastUploadBytes; }

private:
    struct RetiredBuffers {
        uint64_t releaseFrame = 0;
        std::shared_ptr<GpuBuffer> vertex;
        std::shared_ptr<GpuBuffer> index;
    };

    IRHIDevice* m_Device = nullptr;
    std::unordered_map<const MeshAsset*, GpuGeometryAllocation> m_Allocations;
    std::vector<MeshAsset*> m_MeshOrder;
    std::shared_ptr<GpuBuffer> m_VertexBuffer;
    std::shared_ptr<GpuBuffer> m_IndexBuffer;
    std::vector<RetiredBuffers> m_Retired;
    uint64_t m_LastUploadBytes = 0;
};

struct alignas(16) GpuSceneObjectData {
    Mat4 world = Mat4::Identity();
    Mat4 previousWorld = Mat4::Identity();
    Vec4 boundsMin{};
    Vec4 boundsMax{};
    uint32_t meshId = 0;
    uint32_t materialId = 0;
    uint32_t bonePaletteOffset = UINT32_MAX;
    uint32_t flags = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int32_t baseVertex = 0;
    uint32_t objectId = 0;
};

struct alignas(16) GpuSceneLightData {
    Vec4 positionRange{};
    Vec4 directionType{};
    Vec4 colorIntensity{};
    Vec4 spotAnglesShadow{};
};

// The low three bits are a stable CPU/HLSL ABI: filter, U clamp, then V clamp. TextureFilter::Nearest is the only
// point-filtered mode, while Mirror follows the existing material sampler fallback and is treated as Repeat.
inline constexpr uint32_t kGpuSceneMaterialSamplerPointBit = 1u << 0u;
inline constexpr uint32_t kGpuSceneMaterialSamplerClampUBit = 1u << 1u;
inline constexpr uint32_t kGpuSceneMaterialSamplerClampVBit = 1u << 2u;
inline constexpr uint32_t kGpuSceneMaterialSamplerCount = 8u;

inline constexpr uint32_t EncodeGpuSceneMaterialSampler(TextureFilter filter, TextureWrap wrapU, TextureWrap wrapV) {
    return (filter == TextureFilter::Nearest ? kGpuSceneMaterialSamplerPointBit : 0u) |
           (wrapU == TextureWrap::Clamp ? kGpuSceneMaterialSamplerClampUBit : 0u) |
           (wrapV == TextureWrap::Clamp ? kGpuSceneMaterialSamplerClampVBit : 0u);
}

struct alignas(16) GpuSceneMaterialData {
    Vec4 baseColor{1, 1, 1, 1};
    Vec4 material{0, 0.5f, 1, 0.5f};
    Vec4 emissive{};
    uint32_t textureIndices0[4]{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    uint32_t samplerIndices0[4]{};
    uint32_t textureIndex4 = UINT32_MAX;
    uint32_t samplerIndex4 = 0;
    uint32_t flags = 0;
    uint32_t padding = 0;

    void SetTextureBinding(uint32_t slot, uint32_t textureIndex, uint32_t samplerIndex) {
        if (slot < 4u) {
            textureIndices0[slot] = textureIndex;
            samplerIndices0[slot] = samplerIndex;
        } else if (slot == 4u) {
            textureIndex4 = textureIndex;
            samplerIndex4 = samplerIndex;
        }
    }
    uint32_t GetSamplerIndex(uint32_t slot) const {
        return slot < 4u ? samplerIndices0[slot] : slot == 4u ? samplerIndex4 : 0u;
    }
};

static_assert(std::is_standard_layout_v<GpuSceneMaterialData>);
static_assert(offsetof(GpuSceneMaterialData, textureIndices0) == 48u);
static_assert(offsetof(GpuSceneMaterialData, samplerIndices0) == 64u);
static_assert(offsetof(GpuSceneMaterialData, textureIndex4) == 80u);
static_assert(offsetof(GpuSceneMaterialData, samplerIndex4) == 84u);
static_assert(offsetof(GpuSceneMaterialData, flags) == 88u);
static_assert(sizeof(GpuSceneMaterialData) == 96u);

struct GpuSceneDatabaseStats {
    uint32_t candidateObjects = 0;
    uint32_t localLights = 0;
    uint32_t dirtyObjectRanges = 0;
    uint32_t dirtyLightRanges = 0;
    uint32_t dirtyMaterialRanges = 0;
    uint64_t uploadBytes = 0;
    uint64_t geometryUploadBytes = 0;
    uint32_t materialResolves = 0;
    uint32_t materialCacheHits = 0;
    uint32_t texturedMaterials = 0;
    uint32_t compatibilityObjects = 0;
    bool extractionReused = false;
    bool candidateBudgetExceeded = false;
    bool lightBudgetExceeded = false;
};

class GpuSceneDatabase {
public:
    static constexpr uint32_t kMaxCandidateObjects = 65536;
    static constexpr uint32_t kMaxLocalLights = 4096;

    explicit GpuSceneDatabase(IRHIDevice* device = nullptr);
    void SetDevice(IRHIDevice* device);
    bool Update(const Scene& scene, uint64_t frameNumber);
    void ResetTemporalState();

    const std::vector<GpuSceneObjectData>& GetObjects() const { return m_Objects; }
    const std::vector<GpuSceneLightData>& GetLights() const { return m_Lights; }
    const std::vector<GpuSceneMaterialData>& GetMaterials() const { return m_Materials; }
    const std::shared_ptr<GpuBuffer>& GetObjectBuffer() const { return m_ObjectBuffer; }
    const std::shared_ptr<GpuBufferView>& GetObjectView() const { return m_ObjectView; }
    const std::shared_ptr<GpuBuffer>& GetLightBuffer() const { return m_LightBuffer; }
    const std::shared_ptr<GpuBufferView>& GetLightView() const { return m_LightView; }
    const std::shared_ptr<GpuBuffer>& GetMaterialBuffer() const { return m_MaterialBuffer; }
    const std::shared_ptr<GpuBufferView>& GetMaterialView() const { return m_MaterialView; }
    std::shared_ptr<GpuSampler> GetMaterialSampler() { return m_MaterialResources.GetLinearSampler(); }
    GpuGeometryArena& GetGeometryArena() { return m_GeometryArena; }
    const GpuSceneDatabaseStats& GetStats() const { return m_Stats; }

private:
    struct CachedMaterial {
        bool valid = false;
        std::weak_ptr<void> materialLifetime;
        uint64_t materialVersion = 0;
        ShaderAssetHandle shader;
        uint64_t shaderVersion = 0;
        std::vector<std::pair<MaterialHandle, uint64_t>> parentVersions;
        std::array<TextureHandle, 5> textures;
        std::array<uint64_t, 5> textureVersions{};
        std::array<void*, 5> textureGpuHandles{};
        GpuSceneMaterialData gpu;
        bool transparent = false;
    };
    struct ObjectKey {
        const Actor* actor = nullptr;
        uint32_t subMesh = 0;
        bool operator==(const ObjectKey& rhs) const { return actor == rhs.actor && subMesh == rhs.subMesh; }
    };
    struct ObjectKeyHash {
        size_t operator()(const ObjectKey& key) const {
            return std::hash<const Actor*>{}(key.actor) ^ (static_cast<size_t>(key.subMesh) << 1u);
        }
    };

    bool EnsureBuffers(uint32_t objectCount, uint32_t lightCount, uint32_t materialCount);
    void UploadDirtyRanges(const std::vector<GpuSceneObjectData>& previousObjects,
                           const std::vector<GpuSceneLightData>& previousLights);

    IRHIDevice* m_Device = nullptr;
    GpuGeometryArena m_GeometryArena;
    std::vector<GpuSceneObjectData> m_Objects;
    std::vector<GpuSceneLightData> m_Lights;
    std::vector<GpuSceneMaterialData> m_Materials;
    using PreviousWorldMap = std::unordered_map<ObjectKey, Mat4, ObjectKeyHash>;
    std::unordered_map<uint64_t, PreviousWorldMap> m_PreviousWorldByScene;
    std::unordered_map<const MeshAsset*, uint32_t> m_MeshIds;
    std::unordered_map<const MaterialAsset*, uint32_t> m_MaterialIds;
    std::unordered_map<const MaterialAsset*, CachedMaterial> m_MaterialCache;
    std::shared_ptr<GpuBuffer> m_ObjectBuffer;
    std::shared_ptr<GpuBufferView> m_ObjectView;
    std::shared_ptr<GpuBuffer> m_LightBuffer;
    std::shared_ptr<GpuBufferView> m_LightView;
    std::shared_ptr<GpuBuffer> m_MaterialBuffer;
    std::shared_ptr<GpuBufferView> m_MaterialView;
    uint32_t m_ObjectCapacity = 0;
    uint32_t m_LightCapacity = 0;
    uint32_t m_MaterialCapacity = 0;
    MaterialResourceCache m_MaterialResources;
    GpuSceneDatabaseStats m_Stats;
    uint64_t m_LastUpdateSceneGeneration = 0;
    uint64_t m_LastUpdateFrame = UINT64_MAX;
};
