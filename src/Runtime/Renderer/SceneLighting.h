#pragma once

#include "Core/EngineMath.h"

#include <cstdint>
#include <vector>

class Scene;

struct ScenePointLight {
    Vec3 position = Vec3::Zero();
    Vec3 color = Vec3::One();
    float intensity = 1.0f;
    float range = 8.0f;
    float shadowIntensity = 1.0f;
};

struct SceneSpotLight {
    Vec3 position = Vec3::Zero();
    Vec3 direction = Vec3{0.0f, -1.0f, 0.0f};
    Vec3 color = Vec3::One();
    float intensity = 1.0f;
    float range = 8.0f;
    float innerConeCos = 0.9f;
    float outerConeCos = 0.8f;
    float shadowIntensity = 1.0f;
};

struct SceneLightData {
    Vec3 direction = Vec3{-0.55f, -1.0f, -0.45f}.Normalized();
    Vec3 color = Vec3::One();
    float directionalIntensity = 0.0f;
    float directionalShadowIntensity = 1.0f;
    float ambientIntensity = 1.0f;
    Vec3 environmentColor = Vec3::One();
    float skyIntensity = 1.0f;
    Vec3 skyTint = Vec3::One();
    Vec3 horizonTint = Vec3::One();
    Vec3 groundTint = Vec3::One();
    std::vector<ScenePointLight> pointLights;
    std::vector<SceneSpotLight> spotLights;
};

struct SceneEnvironmentData {
    Vec3 environmentColor = Vec3::One();
    float environmentIntensity = 1.0f;
    float skyIntensity = 1.0f;
    Vec3 skyTint = Vec3::One();
    Vec3 horizonTint = Vec3::One();
    Vec3 groundTint = Vec3::One();
    uint64_t sourceActorID = 0;
    uint32_t activeSkylightCount = 0;

    bool HasSkylight() const { return sourceActorID != 0; }
};

struct ScenePostProcessData {
    float exposure = 1.0f;
    float gamma = 2.2f;
    float toneMapping = 1.0f;
    float vignette = 0.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float antiAliasingStrength = 0.0f;
};

SceneEnvironmentData CollectSceneEnvironmentData(const Scene& scene);
SceneLightData CollectSceneLights(const Scene& scene);
SceneLightData CollectSceneLights(const Scene& scene, const SceneEnvironmentData& environment);
ScenePostProcessData CollectScenePostProcessData(const Scene& scene);
