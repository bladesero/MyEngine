#pragma once

#include "Renderer/SceneLighting.h"
#include "Renderer/SceneRenderCollector.h"

#include <vector>

class Camera;
class GpuCommandList;
class MainPass;
class Scene;

struct ForwardRenderContext {
    const SceneLightData* sceneLights = nullptr;
    const ScenePostProcessData* postProcess = nullptr;
    const Mat4* viewProjection = nullptr;
};

class SkyPass {
public:
    explicit SkyPass(MainPass& mainPass);
    void Execute(GpuCommandList& commands, const Camera& camera);

private:
    MainPass& m_MainPass;
};

class ForwardOpaquePass {
public:
    explicit ForwardOpaquePass(MainPass& mainPass);
    void Execute(GpuCommandList& commands, const Scene& scene, const Camera& camera,
                 const std::vector<SceneRenderItem>& items, const ForwardRenderContext& context);

private:
    MainPass& m_MainPass;
};

class ForwardTransparentPass {
public:
    explicit ForwardTransparentPass(MainPass& mainPass);
    void Execute(GpuCommandList& commands, const Scene& scene, const Camera& camera,
                 const std::vector<SceneRenderItem>& items, const ForwardRenderContext& context);

private:
    MainPass& m_MainPass;
};
