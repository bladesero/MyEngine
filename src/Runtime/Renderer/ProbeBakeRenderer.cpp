#include "Renderer/ProbeBakeRenderer.h"

#include "Assets/LightingProbeAsset.h"
#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Math/Mat4Inverse.h"
#include "Renderer/ProbeComponents.h"
#include "Renderer/SceneLighting.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>
#include <vector>

namespace {
constexpr uint32_t kMaxReflectionProbes = 32;
constexpr uint32_t kMaxSHVolumes = 8;
constexpr uint32_t kMaxSamplesPerVolume = 8192;
constexpr uint32_t kMaxSceneSamples = 32768;

struct FloatImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<Vec3> pixels;
};

struct CubeFace {
    FloatImage color;
    std::vector<float> depth;
};

struct FaceBasis {
    Vec3 forward;
    Vec3 right;
    Vec3 up;
};

constexpr std::array<FaceBasis, 6> kFaces = {{
    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
    {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
}};

Vec3 Multiply(const Vec3& a, const Vec3& b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

Vec3 ClampPositive(const Vec3& value) {
    return {(std::max)(0.0f, value.x), (std::max)(0.0f, value.y), (std::max)(0.0f, value.z)};
}

Vec3 SkyRadiance(Vec3 direction, Vec3 sunDirection) {
    direction = direction.Normalized();
    sunDirection = sunDirection.Normalized();
    const float mu = direction.Dot(sunDirection);
    const float sky = std::clamp(direction.y * 20.0f + 0.5f, 0.0f, 1.0f);
    const float horizon = 1.0f - std::clamp(std::abs(direction.y) * 6.0f, 0.0f, 1.0f);
    const float sun = std::pow((std::max)(0.0f, mu), 2048.0f) * 18.0f;
    const Vec3 ground{0.01f, 0.012f, 0.014f};
    const Vec3 zenith{0.08f, 0.18f, 0.42f};
    return ground * (1.0f - sky) + zenith * sky + Vec3{0.055f, 0.075f, 0.105f} * horizon +
           Vec3{24.0f, 20.0f, 14.0f} * sun;
}

Vec3 ShadeSurface(const Vec3& worldPosition, Vec3 normal, const Vec3& baseColor, const Vec3& emissive,
                  const SceneLightData& lights) {
    normal = normal.LengthSq() > 1e-8f ? normal.Normalized() : Vec3::Up();
    Vec3 color = Multiply(baseColor, SkyRadiance(normal, -lights.direction)) * (0.18f * lights.ambientIntensity);
    const Vec3 directional = (-lights.direction).Normalized();
    color += Multiply(baseColor, lights.color) *
             ((std::max)(0.0f, normal.Dot(directional)) * lights.directionalIntensity);
    for (const ScenePointLight& light : lights.pointLights) {
        const Vec3 delta = light.position - worldPosition;
        const float distance = delta.Length();
        if (distance >= light.range || distance <= 1e-5f)
            continue;
        const float attenuation = std::pow(1.0f - distance / light.range, 2.0f);
        color += Multiply(baseColor, light.color) *
                 ((std::max)(0.0f, normal.Dot(delta / distance)) * light.intensity * attenuation);
    }
    for (const SceneSpotLight& light : lights.spotLights) {
        const Vec3 delta = light.position - worldPosition;
        const float distance = delta.Length();
        if (distance >= light.range || distance <= 1e-5f)
            continue;
        const Vec3 toLight = delta / distance;
        const float cone = std::clamp(((-toLight).Dot(light.direction) - light.outerConeCos) /
                                          (std::max)(light.innerConeCos - light.outerConeCos, 1e-5f),
                                      0.0f, 1.0f);
        const float attenuation = std::pow(1.0f - distance / light.range, 2.0f) * cone;
        color += Multiply(baseColor, light.color) *
                 ((std::max)(0.0f, normal.Dot(toLight)) * light.intensity * attenuation);
    }
    return ClampPositive(color + emissive);
}

bool ProjectVertex(const Vec3& position, const Vec3& capture, const FaceBasis& face, uint32_t resolution,
                   Vec3& projected) {
    const Vec3 delta = position - capture;
    const float depth = delta.Dot(face.forward);
    if (depth <= 0.01f)
        return false;
    const float x = delta.Dot(face.right) / depth;
    const float y = delta.Dot(face.up) / depth;
    projected = {(x * 0.5f + 0.5f) * static_cast<float>(resolution),
                 (0.5f - y * 0.5f) * static_cast<float>(resolution), depth};
    return true;
}

float Edge(const Vec3& a, const Vec3& b, float x, float y) {
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

void RasterizeTriangle(CubeFace& target, const Vec3 projected[3], const Vec3 world[3], const Vec3 normal[3],
                       const Vec3& baseColor, const Vec3& emissive, const SceneLightData& lights) {
    const float area = Edge(projected[0], projected[1], projected[2].x, projected[2].y);
    if (std::abs(area) < 1e-6f)
        return;
    const int maxXLimit = static_cast<int>(target.color.width) - 1;
    const int maxYLimit = static_cast<int>(target.color.height) - 1;
    const int minX = std::clamp(static_cast<int>(std::floor((std::min)({projected[0].x, projected[1].x,
                                                                        projected[2].x}))),
                                0, maxXLimit);
    const int maxX = std::clamp(static_cast<int>(std::ceil((std::max)({projected[0].x, projected[1].x,
                                                                       projected[2].x}))),
                                0, maxXLimit);
    const int minY = std::clamp(static_cast<int>(std::floor((std::min)({projected[0].y, projected[1].y,
                                                                        projected[2].y}))),
                                0, maxYLimit);
    const int maxY = std::clamp(static_cast<int>(std::ceil((std::max)({projected[0].y, projected[1].y,
                                                                       projected[2].y}))),
                                0, maxYLimit);
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const float w0 = Edge(projected[1], projected[2], px, py) / area;
            const float w1 = Edge(projected[2], projected[0], px, py) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;
            const float invDepth = w0 / projected[0].z + w1 / projected[1].z + w2 / projected[2].z;
            if (invDepth <= 0.0f)
                continue;
            const float depth = 1.0f / invDepth;
            const size_t pixel = static_cast<size_t>(y) * target.color.width + x;
            if (depth >= target.depth[pixel])
                continue;
            const float p0 = w0 / projected[0].z * depth;
            const float p1 = w1 / projected[1].z * depth;
            const float p2 = w2 / projected[2].z * depth;
            target.depth[pixel] = depth;
            const Vec3 worldPosition = world[0] * p0 + world[1] * p1 + world[2] * p2;
            const Vec3 worldNormal = normal[0] * p0 + normal[1] * p1 + normal[2] * p2;
            target.color.pixels[pixel] = ShadeSurface(worldPosition, worldNormal, baseColor, emissive, lights);
        }
}

std::array<CubeFace, 6> CaptureScene(const Scene& scene, const Vec3& capture, uint32_t resolution,
                                     const SceneLightData& lights) {
    std::array<CubeFace, 6> faces;
    for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
        auto& face = faces[faceIndex];
        face.color.width = face.color.height = resolution;
        face.color.pixels.resize(static_cast<size_t>(resolution) * resolution);
        face.depth.assign(face.color.pixels.size(), std::numeric_limits<float>::infinity());
        for (uint32_t y = 0; y < resolution; ++y)
            for (uint32_t x = 0; x < resolution; ++x) {
                const float sx = (static_cast<float>(x) + 0.5f) / resolution * 2.0f - 1.0f;
                const float sy = 1.0f - (static_cast<float>(y) + 0.5f) / resolution * 2.0f;
                const Vec3 direction = (kFaces[faceIndex].forward + kFaces[faceIndex].right * sx +
                                        kFaces[faceIndex].up * sy)
                                           .Normalized();
                face.color.pixels[static_cast<size_t>(y) * resolution + x] =
                    SkyRadiance(direction, -lights.direction);
            }
    }
    scene.ForEach([&](Actor& actor) {
        if (!actor.IsStatic())
            return;
        const auto* renderer = actor.GetComponent<MeshRendererComponent>();
        if (!renderer || !renderer->GetMesh().IsValid())
            return;
        const MeshAsset& mesh = *renderer->GetMesh();
        const auto& vertices = mesh.GetVertices();
        const auto& indices = mesh.GetIndices();
        const Mat4 worldMatrix = actor.GetWorldMatrix();
        Mat4 normalMatrix = Mat4::Identity();
        if (Mat4Invert(worldMatrix, normalMatrix))
            normalMatrix = normalMatrix.Transposed();
        for (const SubMesh& subMesh : mesh.GetSubMeshes()) {
            MaterialHandle material = renderer->GetMaterialForSlot(subMesh.materialSlot);
            if (!material.IsValid() || material->GetBlendMode() == BlendMode::Transparent)
                continue;
            const Vec3 baseColor = material->GetColor("BaseColor", Vec3::One());
            const Vec3 emissive = material->GetColor("EmissiveColor", Vec3::Zero()) *
                                  material->GetFloat("EmissiveIntensity", 1.0f);
            for (uint32_t index = 0; index + 2 < subMesh.indexCount; index += 3) {
                uint32_t vertexIndices[3]{};
                for (uint32_t corner = 0; corner < 3; ++corner) {
                    const uint32_t source = subMesh.indexOffset + index + corner;
                    vertexIndices[corner] = indices.empty() ? source : indices[source];
                    vertexIndices[corner] += subMesh.vertexOffset;
                }
                if (vertexIndices[0] >= vertices.size() || vertexIndices[1] >= vertices.size() ||
                    vertexIndices[2] >= vertices.size())
                    continue;
                Vec3 world[3], normals[3];
                for (uint32_t corner = 0; corner < 3; ++corner) {
                    world[corner] = worldMatrix.TransformPoint(vertices[vertexIndices[corner]].position);
                    normals[corner] = normalMatrix.TransformDir(vertices[vertexIndices[corner]].normal).Normalized();
                }
                for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
                    Vec3 projected[3];
                    if (!ProjectVertex(world[0], capture, kFaces[faceIndex], resolution, projected[0]) ||
                        !ProjectVertex(world[1], capture, kFaces[faceIndex], resolution, projected[1]) ||
                        !ProjectVertex(world[2], capture, kFaces[faceIndex], resolution, projected[2]))
                        continue;
                    RasterizeTriangle(faces[faceIndex], projected, world, normals, baseColor, emissive, lights);
                }
            }
        }
    });
    return faces;
}

Vec3 SampleCube(const std::array<CubeFace, 6>& faces, Vec3 direction) {
    direction = direction.Normalized();
    uint32_t faceIndex = 0;
    const Vec3 absolute{std::abs(direction.x), std::abs(direction.y), std::abs(direction.z)};
    if (absolute.x >= absolute.y && absolute.x >= absolute.z)
        faceIndex = direction.x >= 0.0f ? 0u : 1u;
    else if (absolute.y >= absolute.z)
        faceIndex = direction.y >= 0.0f ? 2u : 3u;
    else
        faceIndex = direction.z >= 0.0f ? 4u : 5u;
    const FaceBasis& face = kFaces[faceIndex];
    const float depth = (std::max)(direction.Dot(face.forward), 1e-5f);
    const float u = direction.Dot(face.right) / depth * 0.5f + 0.5f;
    const float v = 0.5f - direction.Dot(face.up) / depth * 0.5f;
    const uint32_t x = (std::min)(static_cast<uint32_t>(std::clamp(u, 0.0f, 0.999999f) * faces[faceIndex].color.width),
                                  faces[faceIndex].color.width - 1u);
    const uint32_t y = (std::min)(static_cast<uint32_t>(std::clamp(v, 0.0f, 0.999999f) * faces[faceIndex].color.height),
                                  faces[faceIndex].color.height - 1u);
    return faces[faceIndex].color.pixels[static_cast<size_t>(y) * faces[faceIndex].color.width + x];
}

Vec3 OctaDirection(float u, float v) {
    Vec3 direction{u * 2.0f - 1.0f, v * 2.0f - 1.0f, 1.0f};
    direction.z = 1.0f - std::abs(direction.x) - std::abs(direction.y);
    if (direction.z < 0.0f) {
        const float x = direction.x;
        direction.x = (1.0f - std::abs(direction.y)) * (x >= 0.0f ? 1.0f : -1.0f);
        direction.y = (1.0f - std::abs(x)) * (direction.y >= 0.0f ? 1.0f : -1.0f);
    }
    return direction.Normalized();
}

FloatImage CubeToOcta(const std::array<CubeFace, 6>& faces, uint32_t resolution) {
    FloatImage result{resolution, resolution, {}};
    result.pixels.resize(static_cast<size_t>(resolution) * resolution);
    for (uint32_t y = 0; y < resolution; ++y)
        for (uint32_t x = 0; x < resolution; ++x)
            result.pixels[static_cast<size_t>(y) * resolution + x] =
                SampleCube(faces, OctaDirection((x + 0.5f) / resolution, (y + 0.5f) / resolution));
    return result;
}

FloatImage Downsample(const FloatImage& source) {
    FloatImage result{(std::max)(1u, source.width / 2u), (std::max)(1u, source.height / 2u), {}};
    result.pixels.resize(static_cast<size_t>(result.width) * result.height);
    for (uint32_t y = 0; y < result.height; ++y)
        for (uint32_t x = 0; x < result.width; ++x) {
            Vec3 sum = Vec3::Zero();
            for (uint32_t oy = 0; oy < 2; ++oy)
                for (uint32_t ox = 0; ox < 2; ++ox)
                    sum += source.pixels[static_cast<size_t>((std::min)(y * 2u + oy, source.height - 1u)) *
                                             source.width +
                                         (std::min)(x * 2u + ox, source.width - 1u)];
            result.pixels[static_cast<size_t>(y) * result.width + x] = sum * 0.25f;
        }
    return result;
}

float SelectRgbmRange(const FloatImage& image, float maximum, bool& clipped) {
    float peak = 0.0f;
    for (const Vec3& pixel : image.pixels)
        peak = (std::max)({peak, pixel.x, pixel.y, pixel.z});
    float range = 4.0f;
    while (range < peak && range < maximum)
        range *= 2.0f;
    range = std::clamp(range, 4.0f, maximum);
    clipped = peak > range;
    return range;
}

void EncodeRgbm(const FloatImage& image, float range, std::vector<uint8_t>& output) {
    for (const Vec3& pixel : image.pixels) {
        const Vec3 normalized = ClampPositive(pixel) / range;
        const float maximum = (std::max)({normalized.x, normalized.y, normalized.z, 1.0f / 255.0f});
        const float multiplier = std::clamp(std::ceil(maximum * 255.0f) / 255.0f, 1.0f / 255.0f, 1.0f);
        output.push_back(static_cast<uint8_t>(std::clamp(normalized.x / multiplier, 0.0f, 1.0f) * 255.0f + 0.5f));
        output.push_back(static_cast<uint8_t>(std::clamp(normalized.y / multiplier, 0.0f, 1.0f) * 255.0f + 0.5f));
        output.push_back(static_cast<uint8_t>(std::clamp(normalized.z / multiplier, 0.0f, 1.0f) * 255.0f + 0.5f));
        output.push_back(static_cast<uint8_t>(multiplier * 255.0f + 0.5f));
    }
}

void EvalSH(const Vec3& direction, float basis[9]) {
    basis[0] = 0.282095f;
    basis[1] = 0.488603f * direction.y;
    basis[2] = 0.488603f * direction.z;
    basis[3] = 0.488603f * direction.x;
    basis[4] = 1.092548f * direction.x * direction.y;
    basis[5] = 1.092548f * direction.y * direction.z;
    basis[6] = 0.315392f * (3.0f * direction.z * direction.z - 1.0f);
    basis[7] = 1.092548f * direction.x * direction.z;
    basis[8] = 0.546274f * (direction.x * direction.x - direction.y * direction.y);
}

void BakeSHSample(const Vec3& position, const SceneLightData& lights, std::vector<SHCoefficient>& coefficients) {
    constexpr uint32_t sampleCount = 128;
    std::array<SHCoefficient, 9> sh{};
    for (uint32_t sample = 0; sample < sampleCount; ++sample) {
        const float y = 1.0f - 2.0f * (sample + 0.5f) / sampleCount;
        const float radius = std::sqrt((std::max)(0.0f, 1.0f - y * y));
        const float phi = 2.39996323f * sample;
        const Vec3 direction{std::cos(phi) * radius, y, std::sin(phi) * radius};
        Vec3 radiance = SkyRadiance(direction, -lights.direction);
        const Vec3 directional = (-lights.direction).Normalized();
        radiance += lights.color * (lights.directionalIntensity * std::pow((std::max)(0.0f, direction.Dot(directional)), 64.0f));
        for (const ScenePointLight& light : lights.pointLights) {
            const Vec3 delta = light.position - position;
            const float distance = delta.Length();
            if (distance < light.range && distance > 1e-5f) {
                const float attenuation = std::pow(1.0f - distance / light.range, 2.0f);
                radiance += light.color * (light.intensity * attenuation *
                    std::pow((std::max)(0.0f, direction.Dot(delta / distance)), 32.0f));
            }
        }
        float basis[9];
        EvalSH(direction, basis);
        const float weight = 4.0f * kPi / sampleCount;
        for (uint32_t coefficient = 0; coefficient < 9; ++coefficient) {
            sh[coefficient][0] += radiance.x * basis[coefficient] * weight;
            sh[coefficient][1] += radiance.y * basis[coefficient] * weight;
            sh[coefficient][2] += radiance.z * basis[coefficient] * weight;
        }
    }
    coefficients.insert(coefficients.end(), sh.begin(), sh.end());
}

uint32_t GridDimension(float extent, float spacing) {
    return (std::max)(2u, static_cast<uint32_t>(std::floor(extent * 2.0f / spacing)) + 1u);
}
} // namespace

uint64_t ProbeBakeRenderer::ComputeDependencyHash(const Scene& scene) {
    std::string text = SceneSerializer::SaveToString(scene);
    // The baked asset is an output of this hash, not an input. Removing it keeps a
    // freshly written bake current while still hashing settings and probe components.
    try {
        auto document = nlohmann::json::parse(text);
        document.erase("lightingProbeAsset");
        text = document.dump();
    } catch (...) {
        // SceneSerializer produced this text, so this is only a defensive fallback.
    }
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char value : text) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    static constexpr char version[] = "ProbeBakeRenderer-v1";
    for (unsigned char value : version) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

ProbeBakeResult ProbeBakeRenderer::Bake(const Scene& scene, LightingProbeAsset& output,
                                        const ProgressCallback& progress, const std::atomic<bool>* cancellation) const {
    ProbeBakeResult result;
    struct ReflectionSource { Actor* actor; ReflectionProbeComponent* component; };
    struct VolumeSource { Actor* actor; SHProbeVolumeComponent* component; };
    std::vector<ReflectionSource> reflections;
    std::vector<VolumeSource> volumes;
    std::unordered_set<std::string> ids;
    scene.ForEach([&](Actor& actor) {
        if (auto* probe = actor.GetComponent<ReflectionProbeComponent>())
            reflections.push_back({&actor, probe});
        if (auto* volume = actor.GetComponent<SHProbeVolumeComponent>())
            volumes.push_back({&actor, volume});
    });
    if (reflections.size() > kMaxReflectionProbes || volumes.size() > kMaxSHVolumes) {
        result.error = "probe count exceeds the scene budget";
        return result;
    }
    for (const auto& source : reflections)
        if (!ids.insert(source.component->GetProbeId()).second) {
            result.error = "duplicate lighting probe id: " + source.component->GetProbeId();
            return result;
        }
    for (const auto& source : volumes)
        if (!ids.insert(source.component->GetProbeId()).second) {
            result.error = "duplicate lighting probe id: " + source.component->GetProbeId();
            return result;
        }
    uint32_t totalSHSamples = 0;
    for (const auto& source : volumes) {
        const Vec3 extents = source.component->GetBoxExtents();
        const float spacing = source.component->GetGridSpacing();
        const uint64_t count = static_cast<uint64_t>(GridDimension(extents.x, spacing)) *
                               GridDimension(extents.y, spacing) * GridDimension(extents.z, spacing);
        if (count > kMaxSamplesPerVolume || totalSHSamples + count > kMaxSceneSamples) {
            result.error = "SH probe grid exceeds the scene sample budget";
            return result;
        }
        totalSHSamples += static_cast<uint32_t>(count);
    }
    const uint32_t totalSteps = static_cast<uint32_t>(reflections.size()) + totalSHSamples;
    uint32_t completed = 0;
    const auto report = [&](const char* stage) {
        if (progress)
            progress({completed, totalSteps, stage});
    };
    output.ReflectionProbes().clear();
    output.SHVolumes().clear();
    output.ReflectionPixels().clear();
    output.SHCoefficients().clear();
    output.SetReflectionResolution(scene.GetLightingProbeBakeSettings().reflectionResolution);
    output.SetDependencyHash(ComputeDependencyHash(scene));
    output.SetSceneGuid(scene.GetName());
    const SceneLightData lights = CollectSceneLights(scene);
    for (const auto& source : reflections) {
        if (cancellation && cancellation->load()) {
            result.cancelled = true;
            return result;
        }
        report("Capturing reflection probe");
        const Vec3 capture = source.actor->GetWorldMatrix().TransformPoint(source.component->GetCaptureOffset());
        auto faces = CaptureScene(scene, capture, output.GetReflectionResolution(), lights);
        FloatImage mip = CubeToOcta(faces, output.GetReflectionResolution());
        bool clipped = false;
        const float range = SelectRgbmRange(mip, scene.GetLightingProbeBakeSettings().rgbmMaximumRange, clipped);
        if (clipped)
            ++result.clippedReflectionProbes;
        BakedReflectionProbe baked;
        baked.probeId = source.component->GetProbeId();
        baked.worldPosition = capture;
        baked.boxExtents = source.component->GetBoxExtents();
        baked.rgbmRange = range;
        baked.arrayLayer = static_cast<uint32_t>(output.ReflectionProbes().size());
        output.ReflectionProbes().push_back(baked);
        for (uint32_t level = 0; level < output.GetReflectionMipCount(); ++level) {
            EncodeRgbm(mip, range, output.ReflectionPixels());
            if (level + 1 < output.GetReflectionMipCount())
                mip = Downsample(mip);
        }
        ++completed;
    }
    for (const auto& source : volumes) {
        const Vec3 extents = source.component->GetBoxExtents();
        const float spacing = source.component->GetGridSpacing();
        BakedSHProbeVolume baked;
        baked.probeId = source.component->GetProbeId();
        baked.worldPosition = source.actor->GetWorldPosition();
        baked.boxExtents = extents;
        baked.gridWidth = GridDimension(extents.x, spacing);
        baked.gridHeight = GridDimension(extents.y, spacing);
        baked.gridDepth = GridDimension(extents.z, spacing);
        baked.coefficientOffset = static_cast<uint32_t>(output.SHCoefficients().size());
        output.SHVolumes().push_back(baked);
        for (uint32_t z = 0; z < baked.gridDepth; ++z)
            for (uint32_t y = 0; y < baked.gridHeight; ++y)
                for (uint32_t x = 0; x < baked.gridWidth; ++x) {
                    if (cancellation && cancellation->load()) {
                        result.cancelled = true;
                        return result;
                    }
                    report("Projecting SH probe volume");
                    const Vec3 uvw{baked.gridWidth > 1 ? static_cast<float>(x) / (baked.gridWidth - 1u) : 0.5f,
                                   baked.gridHeight > 1 ? static_cast<float>(y) / (baked.gridHeight - 1u) : 0.5f,
                                   baked.gridDepth > 1 ? static_cast<float>(z) / (baked.gridDepth - 1u) : 0.5f};
                    const Vec3 local{(uvw.x * 2.0f - 1.0f) * extents.x,
                                     (uvw.y * 2.0f - 1.0f) * extents.y,
                                     (uvw.z * 2.0f - 1.0f) * extents.z};
                    BakeSHSample(source.actor->GetWorldMatrix().TransformPoint(local), lights,
                                 output.SHCoefficients());
                    ++completed;
                }
    }
    std::string validationError;
    if (!output.Validate(&validationError)) {
        result.error = validationError;
        return result;
    }
    output.MarkReady();
    result.succeeded = true;
    result.reflectionProbeCount = static_cast<uint32_t>(reflections.size());
    result.shVolumeCount = static_cast<uint32_t>(volumes.size());
    result.shSampleCount = totalSHSamples;
    report("Complete");
    return result;
}
