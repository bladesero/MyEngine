#include "Layer.h"
#include <utility>

Layer::Layer(std::string name)
    : m_Name(std::move(name)) {}
