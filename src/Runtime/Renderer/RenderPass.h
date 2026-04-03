#pragma once

#include "Renderer/IRenderContext.h"
#include "Scene/Scene.h"
#include "Camera/Camera.h"

#include <cstdint>

// Base abstraction for renderer passes (shadow, main color, post, ...).
class RenderPass {
public:
    explicit RenderPass(IRenderContext* context)
        : m_Context(context) {}
    virtual ~RenderPass() = default;

    virtual void Execute(const Scene& scene, const Camera& camera) = 0;
    virtual void Resize(uint32_t width, uint32_t height) {
        (void)width;
        (void)height;
    }

protected:
    IRenderContext* Context() const { return m_Context; }

private:
    IRenderContext* m_Context = nullptr;
};
