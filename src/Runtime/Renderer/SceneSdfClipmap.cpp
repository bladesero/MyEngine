#include "Renderer/SceneSdfClipmap.h"

#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Math/Mat4Inverse.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace {

struct Contributor {
    MeshAsset* mesh = nullptr;
    const MeshSdfVoxelData* data = nullptr;
    Mat4 world;
    Mat4 invWorld;
    AABB worldBounds;
    float localToWorldScale = 1.0f;
};

uint64_t HashBytes(uint64_t seed, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = seed ? seed : 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

template <typename T>
uint64_t HashValue(uint64_t seed, const T& value)
{
    return HashBytes(seed, &value, sizeof(T));
}

uint64_t HashString(uint64_t seed, const std::string& value)
{
    return HashBytes(seed, value.data(), value.size());
}

float Clamp01(float value)
{
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

uint32_t LinearIndex(uint32_t resolution, uint32_t x, uint32_t y, uint32_t z)
{
    return (z * resolution + y) * resolution + x;
}

float ReadMeshSdf(const MeshSdfVoxelData& data, uint32_t x, uint32_t y, uint32_t z)
{
    const uint32_t index = LinearIndex(data.resolution, x, y, z);
    if (index >= data.sdf.size()) return std::numeric_limits<float>::max();
    return static_cast<float>(data.sdf[index]) * data.sdfScale;
}

float SampleMeshSdf(const MeshSdfVoxelData& data, const Vec3& localPoint)
{
    const Vec3 size = data.bounds.max - data.bounds.min;
    if (size.x <= 1e-6f || size.y <= 1e-6f || size.z <= 1e-6f) {
        return std::numeric_limits<float>::max();
    }

    if (!data.bounds.Contains(localPoint)) {
        const Vec3 closest{
            (std::max)(data.bounds.min.x, (std::min)(data.bounds.max.x, localPoint.x)),
            (std::max)(data.bounds.min.y, (std::min)(data.bounds.max.y, localPoint.y)),
            (std::max)(data.bounds.min.z, (std::min)(data.bounds.max.z, localPoint.z))};
        return (localPoint - closest).Length();
    }

    const float fx = Clamp01((localPoint.x - data.bounds.min.x) / size.x) *
        static_cast<float>(data.resolution - 1);
    const float fy = Clamp01((localPoint.y - data.bounds.min.y) / size.y) *
        static_cast<float>(data.resolution - 1);
    const float fz = Clamp01((localPoint.z - data.bounds.min.z) / size.z) *
        static_cast<float>(data.resolution - 1);
    const uint32_t x0 = static_cast<uint32_t>(std::floor(fx));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(fy));
    const uint32_t z0 = static_cast<uint32_t>(std::floor(fz));
    const uint32_t x1 = (std::min)(x0 + 1, data.resolution - 1);
    const uint32_t y1 = (std::min)(y0 + 1, data.resolution - 1);
    const uint32_t z1 = (std::min)(z0 + 1, data.resolution - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float tz = fz - static_cast<float>(z0);

    const float c000 = ReadMeshSdf(data, x0, y0, z0);
    const float c100 = ReadMeshSdf(data, x1, y0, z0);
    const float c010 = ReadMeshSdf(data, x0, y1, z0);
    const float c110 = ReadMeshSdf(data, x1, y1, z0);
    const float c001 = ReadMeshSdf(data, x0, y0, z1);
    const float c101 = ReadMeshSdf(data, x1, y0, z1);
    const float c011 = ReadMeshSdf(data, x0, y1, z1);
    const float c111 = ReadMeshSdf(data, x1, y1, z1);
    const float c00 = c000 + (c100 - c000) * tx;
    const float c10 = c010 + (c110 - c010) * tx;
    const float c01 = c001 + (c101 - c001) * tx;
    const float c11 = c011 + (c111 - c011) * tx;
    const float c0 = c00 + (c10 - c00) * ty;
    const float c1 = c01 + (c11 - c01) * ty;
    return c0 + (c1 - c0) * tz;
}

bool SampleMeshVoxel(const MeshSdfVoxelData& data, const Vec3& localPoint)
{
    if (!data.bounds.Contains(localPoint)) return false;
    const Vec3 size = data.bounds.max - data.bounds.min;
    if (size.x <= 1e-6f || size.y <= 1e-6f || size.z <= 1e-6f) return false;
    const uint32_t x = (std::min)(
        static_cast<uint32_t>(Clamp01((localPoint.x - data.bounds.min.x) / size.x) *
            static_cast<float>(data.resolution)),
        data.resolution - 1);
    const uint32_t y = (std::min)(
        static_cast<uint32_t>(Clamp01((localPoint.y - data.bounds.min.y) / size.y) *
            static_cast<float>(data.resolution)),
        data.resolution - 1);
    const uint32_t z = (std::min)(
        static_cast<uint32_t>(Clamp01((localPoint.z - data.bounds.min.z) / size.z) *
            static_cast<float>(data.resolution)),
        data.resolution - 1);
    return data.IsVoxelOccupied(x, y, z);
}

float ExtractMinWorldScale(const Mat4& world)
{
    const float sx = world.TransformDir(Vec3::Right()).Length();
    const float sy = world.TransformDir(Vec3::Up()).Length();
    const float sz = world.TransformDir(Vec3::Forward()).Length();
    return (std::max)(1e-4f, (std::min)(sx, (std::min)(sy, sz)));
}

bool MeshHasOpaqueMaterial(const MeshRendererComponent& renderer, const MeshAsset& mesh)
{
    for (const SubMesh& subMesh : mesh.GetSubMeshes()) {
        MaterialHandle material = renderer.GetMaterialForSlot(subMesh.materialSlot);
        MaterialAsset* materialAsset = material.Get();
        if (!materialAsset || materialAsset->GetBlendMode() != BlendMode::Transparent) {
            return true;
        }
    }
    return false;
}

std::array<float, 4> Float4(float x, float y, float z, float w)
{
    return {x, y, z, w};
}

void AppendMetadata(SceneSdfClipmapData& output)
{
    output.metadata.clear();
    output.metadata.push_back(Float4(
        output.enabled ? 1.0f : 0.0f,
        static_cast<float>(SceneSdfClipmapData::kLevelCount),
        static_cast<float>(SceneSdfClipmapData::kSdfResolution),
        static_cast<float>(SceneSdfClipmapData::kProbeResolution)));
    for (const SceneSdfClipmapLevel& level : output.levels) {
        output.metadata.push_back(Float4(
            level.bounds.min.x, level.bounds.min.y, level.bounds.min.z,
            level.bounds.max.x - level.bounds.min.x));
        output.metadata.push_back(Float4(
            level.cellSize, level.probeCellSize,
            static_cast<float>(level.sdfOffset),
            static_cast<float>(level.voxelWordOffset)));
        output.metadata.push_back(Float4(
            level.bounds.min.x, level.bounds.min.y, level.bounds.min.z,
            static_cast<float>(level.probeOffset)));
    }
}

} // namespace

const SceneSdfClipmapData& SceneSdfClipmapBuilder::Build(const Scene& scene)
{
    std::vector<Contributor> contributors;
    std::vector<std::string> warnings;
    std::unordered_set<std::string> warningKeys;
    AABB sceneBounds;
    bool hasBounds = false;
    uint64_t hash = 1469598103934665603ull;

    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        auto* renderer = actor.GetComponent<MeshRendererComponent>();
        if (!renderer || !renderer->IsEnabled() || !renderer->IsValid()) return;
        MeshAsset* mesh = renderer->GetMesh().Get();
        if (!mesh || !MeshHasOpaqueMaterial(*renderer, *mesh)) return;

        const Mat4 world = actor.GetWorldMatrix();
        const AABB worldBounds = TransformAABB(mesh->GetAABB(), world);
        if (!hasBounds) {
            sceneBounds = worldBounds;
            hasBounds = true;
        } else {
            sceneBounds.Merge(worldBounds);
        }

        hash = HashValue(hash, actor.GetID());
        hash = HashBytes(hash, world.Data(), sizeof(world.m));
        hash = HashString(hash, mesh->GetName());

        std::string error;
        if (!mesh->LoadSdfVoxelData(&error) || !mesh->GetSdfVoxelData() ||
            !mesh->GetSdfVoxelData()->Valid()) {
            const std::string key = mesh->GetName() + "|missing-sdf";
            if (warningKeys.insert(key).second) {
                warnings.push_back("skipped mesh '" + mesh->GetName() +
                    "': missing or invalid SDF/voxel sidecar");
            }
            return;
        }

        Mat4 invWorld = Mat4::Identity();
        if (!Mat4Invert(world, invWorld)) {
            warnings.push_back("skipped mesh '" + mesh->GetName() +
                "': non-invertible world transform");
            return;
        }

        Contributor contributor;
        contributor.mesh = mesh;
        contributor.data = mesh->GetSdfVoxelData();
        contributor.world = world;
        contributor.invWorld = invWorld;
        contributor.worldBounds = worldBounds;
        contributor.localToWorldScale = ExtractMinWorldScale(world);
        contributors.push_back(contributor);

        const std::string sidecar = mesh->GetSdfVoxelPath().string();
        hash = HashString(hash, sidecar);
    });

    if (contributors.empty() || !hasBounds) {
        if (m_Data.enabled || m_Data.sourceHash != 0) {
            ++m_Data.rebuildCount;
        }
        m_Data.enabled = false;
        m_Data.sourceHash = 0;
        m_Data.contributorCount = 0;
        m_Data.sdf.clear();
        m_Data.voxelWords.clear();
        m_Data.warnings = std::move(warnings);
        AppendMetadata(m_Data);
        return m_Data;
    }

    if (m_Data.enabled && m_Data.sourceHash == hash) {
        m_Data.warnings = std::move(warnings);
        return m_Data;
    }

    SceneSdfClipmapData next;
    next.enabled = true;
    next.sourceHash = hash;
    next.contributorCount = static_cast<uint32_t>(contributors.size());
    next.rebuildCount = m_Data.rebuildCount + 1;
    next.warnings = std::move(warnings);

    const Vec3 center = sceneBounds.Center();
    const Vec3 extents = sceneBounds.Extents();
    float largestExtent = (std::max)(extents.x, (std::max)(extents.y, extents.z));
    largestExtent = (std::max)(largestExtent * 1.1f, 0.5f);

    const uint32_t sdfCellsPerLevel =
        SceneSdfClipmapData::kSdfResolution *
        SceneSdfClipmapData::kSdfResolution *
        SceneSdfClipmapData::kSdfResolution;
    const uint32_t voxelWordsPerLevel = (sdfCellsPerLevel + 31u) / 32u;
    const uint32_t probesPerLevel =
        SceneSdfClipmapData::kProbeResolution *
        SceneSdfClipmapData::kProbeResolution *
        SceneSdfClipmapData::kProbeResolution;
    next.sdf.resize(sdfCellsPerLevel * SceneSdfClipmapData::kLevelCount);
    next.voxelWords.resize(voxelWordsPerLevel * SceneSdfClipmapData::kLevelCount);

    for (uint32_t levelIndex = 0; levelIndex < SceneSdfClipmapData::kLevelCount; ++levelIndex) {
        const uint32_t reverse = SceneSdfClipmapData::kLevelCount - 1u - levelIndex;
        const float extent = largestExtent / static_cast<float>(1u << reverse);
        SceneSdfClipmapLevel& level = next.levels[levelIndex];
        level.bounds = AABB::FromCenterHalfExtents(center, Vec3(extent));
        level.cellSize = (extent * 2.0f) /
            static_cast<float>(SceneSdfClipmapData::kSdfResolution);
        level.probeCellSize = (extent * 2.0f) /
            static_cast<float>(SceneSdfClipmapData::kProbeResolution);
        level.sdfOffset = levelIndex * sdfCellsPerLevel;
        level.voxelWordOffset = levelIndex * voxelWordsPerLevel;
        level.probeOffset = levelIndex * probesPerLevel;

        for (uint32_t z = 0; z < SceneSdfClipmapData::kSdfResolution; ++z) {
            for (uint32_t y = 0; y < SceneSdfClipmapData::kSdfResolution; ++y) {
                for (uint32_t x = 0; x < SceneSdfClipmapData::kSdfResolution; ++x) {
                    const Vec3 worldPoint = level.bounds.min + Vec3(
                        (static_cast<float>(x) + 0.5f) * level.cellSize,
                        (static_cast<float>(y) + 0.5f) * level.cellSize,
                        (static_cast<float>(z) + 0.5f) * level.cellSize);
                    float signedDistance = level.cellSize *
                        static_cast<float>(SceneSdfClipmapData::kSdfResolution);
                    bool occupied = false;
                    for (const Contributor& contributor : contributors) {
                        const Vec3 localPoint = contributor.invWorld.TransformPoint(worldPoint);
                        const float meshDistance =
                            SampleMeshSdf(*contributor.data, localPoint) *
                            contributor.localToWorldScale;
                        if (std::abs(meshDistance) < std::abs(signedDistance)) {
                            signedDistance = meshDistance;
                        }
                        if (contributor.worldBounds.Contains(worldPoint)) {
                            occupied = occupied ||
                                SampleMeshVoxel(*contributor.data, localPoint) ||
                                meshDistance <= 0.0f;
                        }
                    }
                    const uint32_t localIndex = LinearIndex(
                        SceneSdfClipmapData::kSdfResolution, x, y, z);
                    next.sdf[level.sdfOffset + localIndex] = signedDistance;
                    if (occupied) {
                        next.voxelWords[level.voxelWordOffset + localIndex / 32u] |=
                            (1u << (localIndex % 32u));
                    }
                }
            }
        }
    }

    AppendMetadata(next);
    m_Data = std::move(next);
    return m_Data;
}
