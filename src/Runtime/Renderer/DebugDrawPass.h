#pragma once

#include "DebugDraw/DebugDrawCommand.h"
#include "Renderer/RenderPass.h"

#include <cstddef>
#include <memory>
#include <vector>

class DebugDrawPass final : public RenderPass {
public:
    explicit DebugDrawPass(IRHIDevice* device);
    ~DebugDrawPass() override;

    void Execute(GpuCommandList& commands, const Scene& scene, const Camera& camera) override;
    bool Prepare(const std::vector<DebugDrawCommand>& commands, RHIFormat colorFormat, RHIFormat depthFormat,
                 DebugDrawViewMask viewMask = DebugDrawViewMask::All);
    void ExecutePrepared(GpuCommandList& commands, const Camera& camera);

    size_t GetPreparedCommandCount() const;
    size_t GetPreparedDrawCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
