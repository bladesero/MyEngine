#pragma once

#include "Layer.h"
#include <vector>

class LayerStack {
public:
    ~LayerStack();

    void PushLayer(Layer* layer);
    void PopLayer(Layer* layer);

    std::vector<Layer*>& GetLayers() { return m_Layers; }
    const std::vector<Layer*>& GetLayers() const { return m_Layers; }

private:
    std::vector<Layer*> m_Layers;
};
