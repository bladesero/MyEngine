#pragma once

#include "../Core/Layer.h"

struct SDL_Renderer;

class GameLayer : public Layer {
public:
    explicit GameLayer(SDL_Renderer* renderer = nullptr);

    void OnAttach() override;
    void OnDetach() override;
    void OnEvent(Event& event) override;
    void OnUpdate(float dt) override;
    void OnRender() override;

private:
    SDL_Renderer* m_Renderer            = nullptr;
    float         m_SecondAccumulator   = 0.0f;
    int           m_FrameInSecond       = 0;
    // simple demo: cycle clear colour
    float         m_Hue                 = 0.0f;
};
