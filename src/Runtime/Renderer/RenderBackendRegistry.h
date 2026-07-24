#pragma once

#include "API/RuntimeApi.h"

#include "Core/Application.h"
#include "Renderer/IRenderContext.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

MYENGINE_RUNTIME_API std::optional<RenderBackend> ParseRenderBackend(std::string_view value);
using RenderBackendFactory = std::unique_ptr<IRenderContext> (*)();
bool RegisterRenderBackend(RenderBackend backend, RenderBackendFactory factory);
bool IsRenderBackendKnown(RenderBackend backend);
MYENGINE_RUNTIME_API bool IsBackendCompiled(RenderBackend backend);
MYENGINE_RUNTIME_API std::unique_ptr<IRenderContext> CreateRenderContext(RenderBackend backend);
MYENGINE_RUNTIME_API const char* RenderBackendToProjectValue(RenderBackend backend);
MYENGINE_RUNTIME_API const char* RenderBackendToLabel(RenderBackend backend);
MYENGINE_RUNTIME_API std::string AvailableRenderBackendValues();
