#pragma once

#include "Renderer/RHI/GpuShader.h"

#include <cstddef>
#include <string>

bool ReflectDxbcStage(const void* bytecode, size_t byteSize, uint8_t stage,
                      ShaderReflection& reflection, std::string* error = nullptr);
bool ReflectDxbcProgram(const void* vsBytecode, size_t vsSize,
                        const void* psBytecode, size_t psSize,
                        ShaderReflection& reflection, std::string* error = nullptr);

