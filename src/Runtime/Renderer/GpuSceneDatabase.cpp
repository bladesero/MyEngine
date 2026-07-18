#include "Renderer/GpuSceneDatabase.h"

#include "Animation/SkinnedMeshRendererComponent.h"
#include "Assets/AssetManager.h"
#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Core/Logger.h"
#include "Math/Mat4Inverse.h"
#include "Renderer/LightComponent.h"
#include "Renderer/MaterialSystem.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <unordered_set>

namespace {
uint32_t GrowCapacity(uint32_t required, uint32_t minimum) {
    uint32_t capacity = minimum;
    while (capacity < required && capacity <= UINT32_MAX / 2u)
        capacity *= 2u;
    return (std::max)(capacity, required);
}

template <typename T>
void UploadChangedRanges(IRHIDevice& device, const std::shared_ptr<GpuBuffer>& buffer, const std::vector<T>& current,
                         const std::vector<T>& previous, uint32_t& rangeCount, uint64_t& uploadedBytes) {
    if (!buffer || current.empty())
        return;
    uint32_t begin = UINT32_MAX;
    const auto flush = [&](uint32_t end, uint32_t& mutableBegin) {
        if (mutableBegin == UINT32_MAX)
            return;
        const uint64_t offset = static_cast<uint64_t>(mutableBegin) * sizeof(T);
        const uint64_t size = static_cast<uint64_t>(end - mutableBegin) * sizeof(T);
        if (device.UpdateBuffer(buffer, offset, current.data() + mutableBegin, size)) {
            ++rangeCount;
            uploadedBytes += size;
        }
        mutableBegin = UINT32_MAX;
    };
    for (uint32_t index = 0; index < current.size(); ++index) {
        const bool changed = index >= previous.size() || std::memcmp(&current[index], &previous[index], sizeof(T)) != 0;
        if (changed && begin == UINT32_MAX)
            begin = index;
        if (!changed)
            flush(index, begin);
    }
    flush(static_cast<uint32_t>(current.size()), begin);
}
} // namespace

bool GpuGeometryArena::EnsureMeshes(const std::vector<MeshAsset*>& meshes, uint64_t frameNumber) {
    m_LastUploadBytes = 0;
    if (!m_Device)
        return false;
    bool changed = false;
    for (MeshAsset* mesh : meshes) {
        if (!mesh || m_Allocations.count(mesh))
            continue;
        m_MeshOrder.push_back(mesh);
        m_Allocations.emplace(mesh, GpuGeometryAllocation{});
        changed = true;
    }
    if (!changed)
        return true;

    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    for (MeshAsset* mesh : m_MeshOrder) {
        GpuGeometryAllocation allocation;
        allocation.firstVertex = static_cast<uint32_t>(vertices.size());
        allocation.vertexCount = mesh->VertexCount();
        allocation.firstIndex = static_cast<uint32_t>(indices.size());
        allocation.indexCount = mesh->IndexCount();
        vertices.insert(vertices.end(), mesh->GetVertices().begin(), mesh->GetVertices().end());
        for (uint32_t index : mesh->GetIndices())
            indices.push_back(index + allocation.firstVertex);
        m_Allocations[mesh] = allocation;
    }
    if (vertices.empty())
        return true;

    const uint32_t vertexBytes = static_cast<uint32_t>(vertices.size() * sizeof(MeshVertex));
    const uint32_t indexBytes = static_cast<uint32_t>((std::max<size_t>)(indices.size(), 1) * sizeof(uint32_t));
    // Geometry must be created through the typed RHI entry points. In D3D12 these select the
    // native vertex/index buffer classes and final resource states consumed by IASet*; treating
    // the arenas as generic shader buffers leaves invalid state and view metadata for ExecuteIndirect.
    auto vertex = m_Device->CreateVertexBuffer(vertices.data(), vertexBytes, sizeof(MeshVertex));
    const uint32_t emptyIndex = 0;
    auto index = m_Device->CreateIndexBuffer(indices.empty() ? &emptyIndex : indices.data(), indexBytes);
    if (!vertex || !index)
        return false;
    if (m_VertexBuffer || m_IndexBuffer)
        m_Retired.push_back({frameNumber + 3u, std::move(m_VertexBuffer), std::move(m_IndexBuffer)});
    m_VertexBuffer = std::move(vertex);
    m_IndexBuffer = std::move(index);
    m_LastUploadBytes = static_cast<uint64_t>(vertexBytes) + indexBytes;
    return true;
}

void GpuGeometryArena::CollectRetired(uint64_t frameNumber) {
    m_Retired.erase(std::remove_if(m_Retired.begin(), m_Retired.end(),
                                   [&](const RetiredBuffers& retired) { return retired.releaseFrame <= frameNumber; }),
                    m_Retired.end());
}

const GpuGeometryAllocation* GpuGeometryArena::Find(const MeshAsset* mesh) const {
    const auto found = m_Allocations.find(mesh);
    return found == m_Allocations.end() ? nullptr : &found->second;
}

GpuSceneDatabase::GpuSceneDatabase(IRHIDevice* device)
    : m_Device(device), m_GeometryArena(device), m_MaterialResources(device) {
}

void GpuSceneDatabase::SetDevice(IRHIDevice* device) {
    if (m_Device == device)
        return;
    m_Device = device;
    m_GeometryArena.SetDevice(device);
    m_MaterialResources.SetDevice(device);
    m_ObjectBuffer.reset();
    m_ObjectView.reset();
    m_LightBuffer.reset();
    m_LightView.reset();
    m_MaterialBuffer.reset();
    m_MaterialView.reset();
    m_MaterialCache.clear();
    m_PreviousWorldByScene.clear();
    m_LastUpdateSceneGeneration = 0;
    m_LastUpdateFrame = UINT64_MAX;
    m_ObjectCapacity = m_LightCapacity = m_MaterialCapacity = 0;
}

bool GpuSceneDatabase::EnsureBuffers(uint32_t objectCount, uint32_t lightCount, uint32_t materialCount) {
    if (!m_Device)
        return false;
    const uint32_t requiredObjects = (std::max)(objectCount, 1u);
    const uint32_t requiredLights = (std::max)(lightCount, 1u);
    const uint32_t requiredMaterials = (std::max)(materialCount, 1u);
    if (!m_ObjectBuffer || requiredObjects > m_ObjectCapacity) {
        m_ObjectCapacity = GrowCapacity(requiredObjects, 1024);
        RHIBufferDesc desc;
        desc.size = m_ObjectCapacity * sizeof(GpuSceneObjectData);
        desc.stride = sizeof(GpuSceneObjectData);
        desc.usage = RHIResourceUsage::ShaderResource;
        desc.debugName = "GpuSceneObjects";
        m_ObjectBuffer = m_Device->CreateBuffer(desc);
        RHIBufferViewDesc view;
        view.elementCount = m_ObjectCapacity;
        view.usage = RHIResourceUsage::ShaderResource;
        m_ObjectView = m_Device->CreateBufferView(m_ObjectBuffer, view);
        if (!m_ObjectBuffer || !m_ObjectView)
            return false;
    }
    if (!m_LightBuffer || requiredLights > m_LightCapacity) {
        m_LightCapacity = GrowCapacity(requiredLights, 256);
        RHIBufferDesc desc;
        desc.size = m_LightCapacity * sizeof(GpuSceneLightData);
        desc.stride = sizeof(GpuSceneLightData);
        desc.usage = RHIResourceUsage::ShaderResource;
        desc.debugName = "GpuSceneLights";
        m_LightBuffer = m_Device->CreateBuffer(desc);
        RHIBufferViewDesc view;
        view.elementCount = m_LightCapacity;
        view.usage = RHIResourceUsage::ShaderResource;
        m_LightView = m_Device->CreateBufferView(m_LightBuffer, view);
        if (!m_LightBuffer || !m_LightView)
            return false;
    }
    if (!m_MaterialBuffer || requiredMaterials > m_MaterialCapacity) {
        m_MaterialCapacity = GrowCapacity(requiredMaterials, 256);
        RHIBufferDesc desc;
        desc.size = m_MaterialCapacity * sizeof(GpuSceneMaterialData);
        desc.stride = sizeof(GpuSceneMaterialData);
        desc.usage = RHIResourceUsage::ShaderResource;
        desc.debugName = "GpuSceneMaterials";
        m_MaterialBuffer = m_Device->CreateBuffer(desc);
        RHIBufferViewDesc view;
        view.elementCount = m_MaterialCapacity;
        view.usage = RHIResourceUsage::ShaderResource;
        m_MaterialView = m_Device->CreateBufferView(m_MaterialBuffer, view);
        if (!m_MaterialBuffer || !m_MaterialView)
            return false;
    }
    return true;
}

bool GpuSceneDatabase::Update(const Scene& scene, uint64_t frameNumber) {
    const uint64_t sceneGeneration = scene.GetLifetimeGeneration();
    if (m_LastUpdateSceneGeneration == sceneGeneration && m_LastUpdateFrame == frameNumber) {
        m_Stats.extractionReused = true;
        m_Stats.dirtyObjectRanges = 0;
        m_Stats.dirtyLightRanges = 0;
        m_Stats.dirtyMaterialRanges = 0;
        m_Stats.uploadBytes = 0;
        m_Stats.geometryUploadBytes = 0;
        m_Stats.materialResolves = 0;
        m_Stats.materialCacheHits = 0;
        return true;
    }
    m_Stats = {};
    const auto previousObjects = m_Objects;
    const auto previousLights = m_Lights;
    const auto previousMaterials = m_Materials;
    m_Objects.clear();
    m_Lights.clear();
    m_Materials.clear();
    // IDs are indices into this frame's tightly packed GPU buffers, not persistent asset identities. Rebuilding the
    // maps prevents a reloaded scene from inheriting a stale pointer-to-index association and keeps every object and
    // material record self-contained for the immutable RenderExtract.
    m_MeshIds.clear();
    m_MaterialIds.clear();
    std::unordered_map<ObjectKey, Mat4, ObjectKeyHash> currentWorld;
    PreviousWorldMap& previousWorld = m_PreviousWorldByScene[sceneGeneration];
    std::vector<MeshAsset*> meshes;
    std::vector<MeshAsset*> meshesById;
    std::unordered_set<MeshAsset*> uniqueMeshes;
    MaterialSystem materialSystem;
    m_MaterialResources.ResetFrameStats();
    m_MaterialResources.EnsureNamedBindingDefaults();
    std::unordered_set<MaterialAsset*> resolvedMaterialsThisFrame;

    const auto resolveMaterial = [&](MaterialAsset* material) -> const CachedMaterial& {
        if (resolvedMaterialsThisFrame.count(material) != 0)
            return m_MaterialCache.at(material);

        auto& cached = m_MaterialCache[material];
        const bool hasContract = material->HasParent() || material->GetShaderAsset().IsValid();
        const std::weak_ptr<void> materialLifetime = material->GetLifetimeToken();
        const bool sameMaterialLifetime = !cached.materialLifetime.owner_before(materialLifetime) &&
                                          !materialLifetime.owner_before(cached.materialLifetime);
        bool cacheValid = cached.valid && sameMaterialLifetime && cached.materialVersion == material->GetVersion();
        if (cacheValid && cached.shader.Get() && cached.shaderVersion != cached.shader.Get()->GetVersion())
            cacheValid = false;
        if (cacheValid) {
            for (const auto& [parent, version] : cached.parentVersions) {
                if (!parent.Get() || parent.Get()->GetVersion() != version) {
                    cacheValid = false;
                    break;
                }
            }
        }
        if (cacheValid) {
            for (uint32_t textureIndex = 0; textureIndex < cached.textures.size(); ++textureIndex) {
                TextureAsset* texture = cached.textures[textureIndex].Get();
                if (!texture)
                    continue;
                m_MaterialResources.EnsureTextureUploaded(texture);
                // A referenced texture with no GPU handle was deferred by the upload budget. Keep retrying instead
                // of freezing an incomplete material record in the cross-frame cache.
                if (texture->GetVersion() != cached.textureVersions[textureIndex] || !texture->GetGpuHandle() ||
                    texture->GetGpuHandle() != cached.textureGpuHandles[textureIndex] ||
                    cached.gpu.GetSamplerIndex(textureIndex) !=
                        EncodeGpuSceneMaterialSampler(texture->GetFilter(), texture->GetWrapU(), texture->GetWrapV())) {
                    cacheValid = false;
                    break;
                }
            }
        }
        if (cacheValid) {
            ++m_Stats.materialCacheHits;
            resolvedMaterialsThisFrame.insert(material);
            return cached;
        }

        ResolvedMaterial resolvedMaterial;
        if (hasContract) {
            resolvedMaterial = materialSystem.Resolve(*material);
            ++m_Stats.materialResolves;
        }
        GpuSceneMaterialData gpuMaterial;
        const auto property = [&](const char* name, const MaterialParam& fallback) {
            if (resolvedMaterial.valid) {
                if (const MaterialParam* value = resolvedMaterial.FindProperty(name))
                    return *value;
            }
            return material->GetParam(name, fallback);
        };
        const MaterialParam baseColor = property("BaseColor", MaterialParam::FromColor(Vec3::One()));
        gpuMaterial.baseColor = {baseColor.data[0], baseColor.data[1], baseColor.data[2], baseColor.data[3]};
        gpuMaterial.material = {std::clamp(property("Metallic", MaterialParam::FromFloat(0.0f)).data[0], 0.0f, 1.0f),
                                std::clamp(property("Roughness", MaterialParam::FromFloat(0.5f)).data[0], 0.04f, 1.0f),
                                (std::max)(property("AmbientOcclusion", MaterialParam::FromFloat(1.0f)).data[0], 0.0f),
                                resolvedMaterial.valid ? resolvedMaterial.alphaThreshold
                                                       : material->GetAlphaThreshold()};
        const MaterialParam emissive = property("Emissive", MaterialParam::FromColor(Vec3::Zero()));
        gpuMaterial.emissive = {emissive.data[0], emissive.data[1], emissive.data[2], 0.0f};
        const char* textureSlots[] = {"BaseColorMap", "NormalMap", "MetallicRoughnessMap", "OcclusionMap",
                                      "EmissiveMap"};
        cached.textures = {};
        cached.textureVersions = {};
        cached.textureGpuHandles = {};
        for (uint32_t textureIndex = 0; textureIndex < std::size(textureSlots); ++textureIndex) {
            TextureHandle textureHandle = resolvedMaterial.valid
                                              ? resolvedMaterial.FindTexture(textureSlots[textureIndex])
                                              : material->GetTexture(textureSlots[textureIndex]);
            TextureAsset* texture = textureHandle.Get();
            if (!texture)
                continue;
            cached.textures[textureIndex] = textureHandle;
            cached.textureVersions[textureIndex] = texture->GetVersion();
            const uint32_t samplerIndex =
                EncodeGpuSceneMaterialSampler(texture->GetFilter(), texture->GetWrapU(), texture->GetWrapV());
            gpuMaterial.SetTextureBinding(textureIndex, UINT32_MAX, samplerIndex);
            m_MaterialResources.EnsureTextureUploaded(texture);
            auto* gpuTexture = static_cast<GpuTexture*>(texture->GetGpuHandle());
            cached.textureGpuHandles[textureIndex] = gpuTexture;
            if (!gpuTexture)
                continue;
            const uint32_t bindless = m_Device->GetBindlessIndex(m_MaterialResources.GetTextureView(gpuTexture));
            if (bindless == UINT32_MAX)
                continue;
            gpuMaterial.SetTextureBinding(textureIndex, bindless, samplerIndex);
            gpuMaterial.flags |= 1u << textureIndex;
        }
        const BlendMode blendMode = resolvedMaterial.valid ? resolvedMaterial.blendMode : material->GetBlendMode();
        if (blendMode == BlendMode::AlphaTest)
            gpuMaterial.flags |= 1u << 8u;
        const bool twoSided = resolvedMaterial.valid ? resolvedMaterial.twoSided : material->IsTwoSided();
        if (twoSided)
            gpuMaterial.flags |= 1u << 9u;
        cached.materialLifetime = materialLifetime;
        cached.materialVersion = material->GetVersion();
        cached.shader = resolvedMaterial.shader;
        cached.shaderVersion = cached.shader.Get() ? cached.shader.Get()->GetVersion() : 0;
        cached.parentVersions.clear();
        for (const std::string& parentPath : resolvedMaterial.parentChain) {
            MaterialHandle parent = AssetManager::Get().GetByPath<MaterialAsset>(parentPath);
            if (parent.Get() && parent.Get() != material)
                cached.parentVersions.emplace_back(parent, parent.Get()->GetVersion());
        }
        cached.gpu = gpuMaterial;
        cached.transparent = blendMode == BlendMode::Transparent;
        cached.valid = true;
        resolvedMaterialsThisFrame.insert(material);
        return cached;
    };

    const auto addObject = [&](Actor& actor, MeshAsset* mesh, MaterialAsset* material, uint32_t subMeshIndex,
                               bool skinned, const CachedMaterial& cachedMaterial) {
        if (!mesh || !material || subMeshIndex >= mesh->GetSubMeshes().size())
            return;
        // Skinned meshes and authored code/graph passes retain their specialized vertex/material ABI in the
        // compatibility raster subpass. The shared indirect stream contains only standard static surfaces.
        if (skinned || material->HasParent() || material->GetShaderAsset().IsValid()) {
            ++m_Stats.compatibilityObjects;
            return;
        }
        if (m_Objects.size() >= kMaxCandidateObjects) {
            m_Stats.candidateBudgetExceeded = true;
            return;
        }
        if (uniqueMeshes.insert(mesh).second)
            meshes.push_back(mesh);
        const auto meshInsert = m_MeshIds.emplace(mesh, static_cast<uint32_t>(m_MeshIds.size()));
        const uint32_t meshId = meshInsert.first->second;
        if (meshInsert.second)
            meshesById.push_back(mesh);
        const auto materialInsert = m_MaterialIds.emplace(material, static_cast<uint32_t>(m_MaterialIds.size()));
        const uint32_t materialId = materialInsert.first->second;
        if (materialInsert.second)
            m_Materials.push_back(cachedMaterial.gpu);
        const SubMesh& subMesh = mesh->GetSubMeshes()[subMeshIndex];
        const Mat4 world = actor.GetWorldMatrix();
        const ObjectKey key{&actor, subMeshIndex};
        const auto previous = previousWorld.find(key);
        GpuSceneObjectData object;
        object.world = world;
        object.previousWorld = previous == previousWorld.end() ? world : previous->second;
        Mat4 normalMatrix = Mat4::Identity();
        if (Mat4Invert(world, normalMatrix))
            normalMatrix = normalMatrix.Transposed();
        object.normalMatrix = normalMatrix;
        const AABB bounds = TransformAABB(subMesh.bounds, world);
        object.boundsMin = {bounds.min.x, bounds.min.y, bounds.min.z, 1.0f};
        object.boundsMax = {bounds.max.x, bounds.max.y, bounds.max.z, 1.0f};
        object.meshId = meshId;
        object.materialId = materialId;
        object.flags = skinned ? 1u : 0u;
        if (material->GetBlendMode() == BlendMode::AlphaTest)
            object.flags |= 2u;
        object.firstIndex = subMesh.indexOffset;
        object.indexCount = subMesh.indexCount;
        object.objectId = static_cast<uint32_t>(m_Objects.size());
        m_Objects.push_back(object);
        currentWorld[key] = world;
    };

    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive())
            return;
        if (auto* light = actor.GetComponent<LightComponent>();
            light && light->IsEnabled() && light->GetLightType() != LightType::Directional) {
            if (m_Lights.size() >= kMaxLocalLights) {
                m_Stats.lightBudgetExceeded = true;
            } else {
                GpuSceneLightData data;
                const Vec3 position = actor.GetWorldPosition();
                const Vec3 direction = light->GetDirection();
                const Vec3 color = light->GetColor();
                data.positionRange = {position.x, position.y, position.z, light->GetRange()};
                data.directionType = {direction.x, direction.y, direction.z,
                                      light->GetLightType() == LightType::Spot ? 1.0f : 0.0f};
                data.colorIntensity = {color.x, color.y, color.z, light->GetIntensity()};
                constexpr float degreesToRadians = 3.14159265359f / 180.0f;
                data.spotAnglesShadow = {std::cos(light->GetInnerConeAngle() * degreesToRadians),
                                         std::cos(light->GetOuterConeAngle() * degreesToRadians),
                                         light->CastsShadows() ? 1.0f : 0.0f, 0.0f};
                m_Lights.push_back(data);
            }
        }
        if (auto* skinned = actor.GetComponent<SkinnedMeshRendererComponent>();
            skinned && skinned->IsEnabled() && skinned->IsValid()) {
            MeshAsset* mesh = skinned->GetRenderMesh();
            MaterialAsset* material = skinned->GetMaterial().Get();
            if (mesh && material)
                for (uint32_t subMesh = 0; subMesh < mesh->GetSubMeshes().size(); ++subMesh) {
                    const CachedMaterial& cached = resolveMaterial(material);
                    if (!cached.transparent)
                        addObject(actor, mesh, material, subMesh, true, cached);
                }
            return;
        }
        auto* renderer = actor.GetComponent<MeshRendererComponent>();
        MeshAsset* mesh =
            renderer && renderer->IsEnabled() && renderer->IsValid() ? renderer->GetMesh().Get() : nullptr;
        if (!mesh)
            return;
        for (uint32_t subMesh = 0; subMesh < mesh->GetSubMeshes().size(); ++subMesh) {
            MaterialAsset* material = renderer->GetMaterialForSlot(mesh->GetSubMeshes()[subMesh].materialSlot).Get();
            if (!material) {
                ++m_Stats.compatibilityObjects;
                continue;
            }
            const CachedMaterial& cached = resolveMaterial(material);
            if (!cached.transparent)
                addObject(actor, mesh, material, subMesh, false, cached);
        }
    });
    previousWorld = std::move(currentWorld);

    if (m_Stats.candidateBudgetExceeded || m_Stats.lightBudgetExceeded)
        return false;
    if (!m_GeometryArena.EnsureMeshes(meshes, frameNumber) ||
        !EnsureBuffers(static_cast<uint32_t>(m_Objects.size()), static_cast<uint32_t>(m_Lights.size()),
                       static_cast<uint32_t>(m_Materials.size())))
        return false;
    for (auto& object : m_Objects) {
        if (object.meshId >= meshesById.size())
            continue;
        if (const GpuGeometryAllocation* allocation = m_GeometryArena.Find(meshesById[object.meshId])) {
            object.firstIndex += allocation->firstIndex;
            object.baseVertex = 0;
        }
    }
    UploadDirtyRanges(previousObjects, previousLights);
    UploadChangedRanges(*m_Device, m_MaterialBuffer, m_Materials, previousMaterials, m_Stats.dirtyMaterialRanges,
                        m_Stats.uploadBytes);
    m_GeometryArena.CollectRetired(frameNumber);
    m_Stats.candidateObjects = static_cast<uint32_t>(m_Objects.size());
    m_Stats.localLights = static_cast<uint32_t>(m_Lights.size());
    m_Stats.texturedMaterials =
        static_cast<uint32_t>(std::count_if(m_Materials.begin(), m_Materials.end(),
                                            [](const GpuSceneMaterialData& material) { return material.flags & 1u; }));
    m_Stats.geometryUploadBytes = m_GeometryArena.GetLastUploadBytes();
    m_LastUpdateSceneGeneration = sceneGeneration;
    m_LastUpdateFrame = frameNumber;
    return true;
}

void GpuSceneDatabase::UploadDirtyRanges(const std::vector<GpuSceneObjectData>& previousObjects,
                                         const std::vector<GpuSceneLightData>& previousLights) {
    UploadChangedRanges(*m_Device, m_ObjectBuffer, m_Objects, previousObjects, m_Stats.dirtyObjectRanges,
                        m_Stats.uploadBytes);
    UploadChangedRanges(*m_Device, m_LightBuffer, m_Lights, previousLights, m_Stats.dirtyLightRanges,
                        m_Stats.uploadBytes);
}

void GpuSceneDatabase::ResetTemporalState() {
    m_PreviousWorldByScene.clear();
    m_LastUpdateSceneGeneration = 0;
    m_LastUpdateFrame = UINT64_MAX;
}
