#pragma once

#include "Renderer/ProbeBakeRenderer.h"

class EditorContext;
class Scene;

class EditorLightingBakeService {
public:
    ProbeBakeResult Bake(EditorContext& context, Scene& scene) const;
    bool IsBakeCurrent(const Scene& scene) const;
};
