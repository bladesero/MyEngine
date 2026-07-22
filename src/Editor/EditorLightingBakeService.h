#pragma once

#include "Editor/EditorService.h"
#include "Renderer/ProbeBakeRenderer.h"

class Scene;

class EditorLightingBakeService final : public EditorService {
public:
    bool RequestBake(Scene& scene);
    void OnUpdate(float deltaSeconds) override;
    void OnDetach() override;
    bool IsBakeCurrent(const Scene& scene) const;
    bool IsBakePending(const Scene& scene) const { return m_PendingScene == &scene || m_ActiveScene == &scene; }
    const ProbeBakeResult& GetLastResult() const { return m_LastResult; }

private:
    ProbeBakeResult ExecuteBake(EditorContext& context, Scene& scene) const;

    Scene* m_PendingScene = nullptr;
    Scene* m_ActiveScene = nullptr;
    ProbeBakeResult m_LastResult;
};
