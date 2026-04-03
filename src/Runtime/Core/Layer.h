#pragma once

#include "Event.h"
#include <string>

class Layer {
public:
    explicit Layer(std::string name);
    virtual ~Layer() = default;

    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void OnEvent(Event& event) { (void)event; }
    virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }
    virtual void OnRender() {}

    const std::string& Name() const { return m_Name; }

private:
    std::string m_Name;
};
