#include "Renderer/ProbeBakeRenderer.h"

#include "Assets/LightingProbeAsset.h"
#include "Camera/Camera.h"
#include "Renderer/ProbeComponents.h"
#include "Renderer/Renderer.h"
#include "Renderer/RHI/GpuReadback.h"
#include "Renderer/RHI/GpuTextureView.h"
#include "Renderer/RHI/IRHIDevice.h"
#include "Renderer/RHI/IRHIFrameContext.h"
#include "Renderer/RHI/IRHIReadbackService.h"
#include "Renderer/SceneLighting.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
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

Vec3 SkyRadiance(Vec3 direction, Vec3 sunDirection, const SceneLightData& lights) {
    direction = direction.Normalized();
    sunDirection = sunDirection.Normalized();
    const float mu = direction.Dot(sunDirection);
    const float sky = std::clamp(direction.y * 20.0f + 0.5f, 0.0f, 1.0f);
    const float horizon = 1.0f - std::clamp(std::abs(direction.y) * 6.0f, 0.0f, 1.0f);
    const float sun = std::pow((std::max)(0.0f, mu), 2048.0f) * 18.0f;
    const Vec3 ground = Multiply(Vec3{0.01f, 0.012f, 0.014f}, lights.groundTint);
    const Vec3 zenith = Multiply(Vec3{0.08f, 0.18f, 0.42f}, lights.skyTint);
    const Vec3 horizonColor = Multiply(Vec3{0.055f, 0.075f, 0.105f}, lights.horizonTint);
    const Vec3 solar = Multiply(Vec3{24.0f, 20.0f, 14.0f}, lights.skyTint);
    return ground * (1.0f - sky) + zenith * sky + horizonColor * horizon + solar * sun;
}

bool DecodeGpuFace(const std::shared_ptr<GpuTextureReadbackTicket>& ticket, FloatImage& output, std::string& error) {
    if (!ticket || !ticket->IsReady()) {
        error = "GPU reflection probe readback did not complete";
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!ticket->Read(bytes)) {
        error = "GPU reflection probe readback failed";
        return false;
    }
    const uint32_t width = ticket->GetWidth();
    const uint32_t height = ticket->GetHeight();
    const uint32_t rowPitch = ticket->GetRowPitch();
    if (width == 0 || height == 0 || bytes.size() < static_cast<size_t>(rowPitch) * height) {
        error = "GPU reflection probe readback payload is truncated";
        return false;
    }
    output = {width, height, {}};
    output.pixels.resize(static_cast<size_t>(width) * height);
    const RHIFormat format = ticket->GetFormat();
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = bytes.data() + static_cast<size_t>(y) * rowPitch;
        for (uint32_t x = 0; x < width; ++x) {
            Vec3 color = Vec3::Zero();
            if (format == RHIFormat::RGBA16Float) {
                uint16_t channels[4]{};
                std::memcpy(channels, row + static_cast<size_t>(x) * sizeof(channels), sizeof(channels));
                color = {LightingProbeHalfToFloat(channels[0]), LightingProbeHalfToFloat(channels[1]),
                         LightingProbeHalfToFloat(channels[2])};
            } else if (format == RHIFormat::RGBA32Float) {
                float channels[4]{};
                std::memcpy(channels, row + static_cast<size_t>(x) * sizeof(channels), sizeof(channels));
                color = {channels[0], channels[1], channels[2]};
            } else if (format == RHIFormat::RGBA8UNorm || format == RHIFormat::BGRA8UNorm) {
                const uint8_t* channels = row + static_cast<size_t>(x) * 4u;
                constexpr float inverseByte = 1.0f / 255.0f;
                color = format == RHIFormat::BGRA8UNorm
                            ? Vec3{channels[2] * inverseByte, channels[1] * inverseByte, channels[0] * inverseByte}
                            : Vec3{channels[0] * inverseByte, channels[1] * inverseByte, channels[2] * inverseByte};
            } else {
                error = "GPU reflection probe readback returned an unsupported texture format";
                return false;
            }
            output.pixels[static_cast<size_t>(y) * width + x] = ClampPositive(color);
        }
    }
    return true;
}

bool CaptureSceneGpu(IRHIDevice* device, IRHIFrameContext* frameContext, IRHIReadbackService* readbackService,
                     const Scene& scene, const Vec3& capture, uint32_t resolution, float farPlane,
                     std::array<FloatImage, 6>& faces, std::string& error) {
    if (!device || !frameContext || !readbackService) {
        error = "reflection probe baking requires GPU device, frame, and readback services";
        return false;
    }
    if (device->GetBackend() != RHIBackend::D3D11 && device->GetBackend() != RHIBackend::D3D12) {
        error = "GPU reflection probe baking currently supports D3D11 and D3D12";
        return false;
    }

    Renderer renderer(device, frameContext, readbackService);
    renderer.SetRenderPath(RenderPath::Forward);
    renderer.SetOutputOffscreen(true);
    renderer.SetFeatureMask(RendererFeatureMask::Shadows);
    renderer.SetStaticGeometryOnly(true);
    renderer.SetLocalLightingProbesEnabled(false);
    renderer.Resize(resolution, resolution);
    // Probe bakes use one-tap runtime shadow sampling, but a smaller shadow atlas avoids paying the full viewport
    // quality cost six times per probe.
    renderer.SetShadowMapResolution(512);

    std::array<std::shared_ptr<GpuTextureReadbackTicket>, 6> tickets;
    bool frameOpened = false;
    for (uint32_t faceIndex = 0; faceIndex < kFaces.size(); ++faceIndex) {
        Camera camera;
        camera.LookAt(capture, capture + kFaces[faceIndex].forward, kFaces[faceIndex].up);
        camera.SetPerspective(90.0f, 1.0f, 0.05f, (std::max)(farPlane, 10.0f));
        renderer.RenderScene(scene, camera, false);
        frameOpened = true;
        GpuTextureView* output = renderer.GetHdrSceneColorView();
        if (!output || !output->texture) {
            error = "GPU reflection probe renderer did not produce an HDR target";
            break;
        }
        RHITextureRegion region{};
        region.width = resolution;
        region.height = resolution;
        tickets[faceIndex] = readbackService->ReadbackTextureAsync(output->texture, region);
        if (!tickets[faceIndex]) {
            error = "GPU reflection probe renderer could not queue texture readback";
            break;
        }
    }
    if (frameOpened)
        frameContext->EndFrame();
    if (!error.empty())
        return false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (const auto& ticket : tickets) {
        while (ticket && !ticket->IsReady() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!ticket || !ticket->IsReady()) {
            error = "GPU reflection probe readback timed out";
            return false;
        }
    }
    for (uint32_t faceIndex = 0; faceIndex < tickets.size(); ++faceIndex)
        if (!DecodeGpuFace(tickets[faceIndex], faces[faceIndex], error))
            return false;
    return true;
}

Vec3 SampleCube(const std::array<FloatImage, 6>& faces, Vec3 direction) {
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
    const uint32_t x = (std::min)(static_cast<uint32_t>(std::clamp(u, 0.0f, 0.999999f) * faces[faceIndex].width),
                                  faces[faceIndex].width - 1u);
    const uint32_t y = (std::min)(static_cast<uint32_t>(std::clamp(v, 0.0f, 0.999999f) * faces[faceIndex].height),
                                  faces[faceIndex].height - 1u);
    return faces[faceIndex].pixels[static_cast<size_t>(y) * faces[faceIndex].width + x];
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

FloatImage CubeToOcta(const std::array<FloatImage, 6>& faces, uint32_t resolution) {
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
                    sum +=
                        source.pixels[static_cast<size_t>((std::min)(y * 2u + oy, source.height - 1u)) * source.width +
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
        Vec3 radiance = SkyRadiance(direction, -lights.direction, lights);
        const Vec3 directional = (-lights.direction).Normalized();
        radiance += lights.color *
                    (lights.directionalIntensity * std::pow((std::max)(0.0f, direction.Dot(directional)), 64.0f));
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
    static constexpr char version[] = "ProbeBakeRenderer-gpu-v4";
    for (unsigned char value : version) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

ProbeBakeResult ProbeBakeRenderer::Bake(const Scene& scene, LightingProbeAsset& output,
                                        const ProgressCallback& progress, const std::atomic<bool>* cancellation) const {
    ProbeBakeResult result;
    struct ReflectionSource {
        Actor* actor;
        ReflectionProbeComponent* component;
    };
    struct VolumeSource {
        Actor* actor;
        SHProbeVolumeComponent* component;
    };
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
    if (!reflections.empty() && (!m_Device || !m_FrameContext || !m_ReadbackService)) {
        result.error = "reflection probes require the GPU bake path";
        return result;
    }
    for (const auto& source : reflections) {
        if (cancellation && cancellation->load()) {
            result.cancelled = true;
            return result;
        }
        report("GPU capturing reflection probe with fast shadows");
        const Vec3 capture = source.actor->GetWorldMatrix().TransformPoint(source.component->GetCaptureOffset());
        std::array<FloatImage, 6> faces;
        const Vec3 extents = source.component->GetBoxExtents();
        const float farPlane = (std::max)(100.0f, extents.Length() * 4.0f);
        if (!CaptureSceneGpu(m_Device, m_FrameContext, m_ReadbackService, scene, capture,
                             output.GetReflectionResolution(), farPlane, faces, result.error))
            return result;
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
                    const Vec3 local{(uvw.x * 2.0f - 1.0f) * extents.x, (uvw.y * 2.0f - 1.0f) * extents.y,
                                     (uvw.z * 2.0f - 1.0f) * extents.z};
                    BakeSHSample(source.actor->GetWorldMatrix().TransformPoint(local), lights, output.SHCoefficients());
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
