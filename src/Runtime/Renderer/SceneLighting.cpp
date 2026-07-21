#include "Renderer/SceneLighting.h"

#include "Renderer/LightComponent.h"
#include "Renderer/PostProcessComponent.h"
#include "Renderer/SkylightComponent.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#include <cmath>

SceneEnvironmentData CollectSceneEnvironmentData(const Scene& scene) {
    SceneEnvironmentData out;
    out.environmentIntensity = scene.GetAmbientIntensity();
    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive())
            return;
        const auto* skylight = actor.GetComponent<SkylightComponent>();
        if (!skylight || !skylight->IsEnabled())
            return;

        ++out.activeSkylightCount;
        if (out.HasSkylight())
            return;

        out.sourceActorID = actor.GetID();
        out.environmentColor = skylight->GetEnvironmentColor();
        out.environmentIntensity = skylight->GetEnvironmentIntensity();
        out.skyIntensity = skylight->GetSkyIntensity();
        out.skyTint = skylight->GetSkyTint();
        out.horizonTint = skylight->GetHorizonTint();
        out.groundTint = skylight->GetGroundTint();
    });
    return out;
}

SceneLightData CollectSceneLights(const Scene& scene, const SceneEnvironmentData& environment) {
    SceneLightData out;
    out.ambientIntensity = environment.environmentIntensity;
    out.environmentColor = environment.environmentColor;
    out.skyIntensity = environment.skyIntensity;
    out.skyTint = environment.skyTint;
    out.horizonTint = environment.horizonTint;
    out.groundTint = environment.groundTint;
    bool foundDirectional = false;
    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive())
            return;
        auto* light = actor.GetComponent<LightComponent>();
        if (!light || !light->IsEnabled())
            return;

        if (light->GetLightType() == LightType::Directional) {
            if (!foundDirectional) {
                out.direction = light->GetDirection();
                out.color = light->GetColor();
                out.directionalIntensity = light->GetIntensity();
                out.directionalShadowIntensity = light->GetShadowIntensity();
                foundDirectional = true;
            }
            return;
        }

        if (light->GetLightType() == LightType::Point && out.pointLights.size() < 4) {
            ScenePointLight point;
            point.position = actor.GetWorldPosition();
            point.color = light->GetColor();
            point.intensity = light->GetIntensity();
            point.range = light->GetRange();
            point.shadowIntensity = light->GetShadowIntensity();
            out.pointLights.push_back(point);
            return;
        }

        if (light->GetLightType() == LightType::Spot && out.spotLights.size() < 4) {
            SceneSpotLight spot;
            spot.position = actor.GetWorldPosition();
            spot.direction = light->GetDirection();
            spot.color = light->GetColor();
            spot.intensity = light->GetIntensity();
            spot.range = light->GetRange();
            spot.innerConeCos = std::cos(light->GetInnerConeAngle() * kDeg2Rad);
            spot.outerConeCos = std::cos(light->GetOuterConeAngle() * kDeg2Rad);
            spot.shadowIntensity = light->GetShadowIntensity();
            out.spotLights.push_back(spot);
        }
    });
    return out;
}

SceneLightData CollectSceneLights(const Scene& scene) {
    return CollectSceneLights(scene, CollectSceneEnvironmentData(scene));
}

ScenePostProcessData CollectScenePostProcessData(const Scene& scene) {
    ScenePostProcessData out;
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive())
            return;
        auto* post = actor.GetComponent<PostProcessComponent>();
        if (!post || !post->IsEnabled())
            return;
        out.exposure = post->GetExposure();
        out.gamma = post->GetGamma();
        out.toneMapping = post->IsToneMappingEnabled() ? 1.0f : 0.0f;
        out.vignette = post->GetVignette();
        out.saturation = post->GetSaturation();
        out.contrast = post->GetContrast();
        out.antiAliasingStrength = post->GetAntiAliasingStrength();
        found = true;
    });
    return out;
}
