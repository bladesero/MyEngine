#include "LayerStack.h"
#include <algorithm>

LayerStack::~LayerStack() {
    Clear();
}

void LayerStack::Clear() {
    for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it) {
        (*it)->OnDetach();
        delete *it;
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
