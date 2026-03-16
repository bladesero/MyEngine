#pragma once

#include "../Core/Layer.h"

class GameLayer : public Layer {
public:
    GameLayer();

    void OnAttach() override;
    void OnDetach() override;
    void OnEvent(Event& event) override;
    void OnUpdate(float dt) override;

private:
    float m_SecondAccumulator = 0.0f;
    int m_FrameInSecond = 0;
};
