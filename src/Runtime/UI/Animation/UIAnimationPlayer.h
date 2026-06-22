#pragma once

#include "UI/Animation/UITween.h"

#include <vector>

class UIAnimationPlayer {
public:
    UITween& AddTween()
    {
        m_Tweens.emplace_back();
        return m_Tweens.back();
    }

    void Update(float dt)
    {
        for (auto& tween : m_Tweens) tween.Update(dt);
    }

    void Clear() { m_Tweens.clear(); }

private:
    std::vector<UITween> m_Tweens;
};
