#pragma once

#include <memory>

class IRHIContext;
using IRenderContext = IRHIContext;

std::unique_ptr<IRenderContext> CreateMetalContext();
