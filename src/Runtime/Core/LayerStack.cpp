#include "LayerStack.h"
#include <algorithm>

LayerStack::~LayerStack() {
    Clear();
}

void LayerStack::Clear() {
    for (Layer* layer : m_Layers) {
        layer->OnDetach();
        delete layer;
    }
    m_Layers.clear();
}

void LayerStack::PushLayer(Layer* layer) {
    m_Layers.push_back(layer);
    layer->OnAttach();
}

void LayerStack::PopLayer(Layer* layer) {
    const auto it = std::find(m_Layers.begin(), m_Layers.end(), layer);
    if (it == m_Layers.end()) {
        return;
    }
    (*it)->OnDetach();
    delete *it;
    m_Layers.erase(it);
}
