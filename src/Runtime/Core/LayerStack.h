#pragma once

#include "API/RuntimeApi.h"

#include "Layer.h"
#include <vector>

class MYENGINE_RUNTIME_API LayerStack {
public:
    ~LayerStack();

    void PushLayer(Layer* layer);
    void PopLayer(Layer* layer);
    void Clear();

    std::vector<Layer*>& GetLayers() { return m_Layers; }
    const std::vector<Layer*>& GetLayers() const { return m_Layers; }

private:
    std::vector<Layer*> m_Layers;
};
