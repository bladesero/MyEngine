#pragma once

#include "Core/Application.h"
#include "Renderer/IRenderContext.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

std::optional<RenderBackend> ParseRenderBackend(std::string_view value);
bool IsRenderBackendKnown(RenderBackend backend);
bool IsBackendCompiled(RenderBackend backend);
std::unique_ptr<IRenderContext> CreateRenderContext(RenderBackend backend);
const char* RenderBackendToProjectValue(RenderBackend backend);
const char* RenderBackendToLabel(RenderBackend backend);
std::string AvailableRenderBackendValues();
