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
    scene.ForEachWith<SkylightComponent>([&](const Actor& actor, const SkylightComponent& skylight) {
        if (!actor.IsActive())
            return;
        if (!skylight.IsEnabled())
            return;

        ++out.activeSkylightCount;
        if (out.HasSkylight())
            return;

        out.sourceActorID = actor.GetID();
        out.environmentColor = skylight.GetEnvironmentColor();
        out.environmentIntensity = skylight.GetEnvironmentIntensity();
        out.skyIntensity = skylight.GetSkyIntensity();
        out.skyTint = skylight.GetSkyTint();
        out.horizonTint = skylight.GetHorizonTint();
        out.groundTint = skylight.GetGroundTint();
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
    scene.ForEachWith<LightComponent>([&](const Actor& actor, const LightComponent& light) {
        if (!actor.IsActive())
            return;
        if (!light.IsEnabled())
            return;

        if (light.GetLightType() == LightType::Directional) {
            if (!foundDirectional) {
                out.direction = light.GetDirection();
                out.color = light.GetColor();
                out.directionalIntensity = light.GetIntensity();
                out.directionalShadowIntensity = light.GetShadowIntensity();
                foundDirectional = true;
            }
            return;
        }

        if (light.GetLightType() == LightType::Point && out.pointLights.size() < 4) {
            ScenePointLight point;
            point.position = actor.GetWorldPosition();
            point.color = light.GetColor();
            point.intensity = light.GetIntensity();
            point.range = light.GetRange();
            point.shadowIntensity = light.GetShadowIntensity();
            out.pointLights.push_back(point);
            return;
        }

        if (light.GetLightType() == LightType::Spot && out.spotLights.size() < 4) {
            SceneSpotLight spot;
            spot.position = actor.GetWorldPosition();
            spot.direction = light.GetDirection();
            spot.color = light.GetColor();
            spot.intensity = light.GetIntensity();
            spot.range = light.GetRange();
            spot.innerConeCos = std::cos(light.GetInnerConeAngle() * kDeg2Rad);
            spot.outerConeCos = std::cos(light.GetOuterConeAngle() * kDeg2Rad);
            spot.shadowIntensity = light.GetShadowIntensity();
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
    scene.ForEachWith<PostProcessComponent>([&](const Actor& actor, const PostProcessComponent& post) {
        if (!actor.IsActive() || !post.IsEnabled())
            return SceneQueryControl::Continue;
        out.exposure = post.GetExposure();
        out.gamma = post.GetGamma();
        out.toneMapping = post.IsToneMappingEnabled() ? 1.0f : 0.0f;
        out.vignette = post.GetVignette();
        out.saturation = post.GetSaturation();
        out.contrast = post.GetContrast();
        out.antiAliasingStrength = post.GetAntiAliasingStrength();
        return SceneQueryControl::Break;
    });
    return out;
}
