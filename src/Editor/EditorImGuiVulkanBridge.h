#pragma once

#include "Core/Platform.h"

#if defined(MYENGINE_PLATFORM_WINDOWS)

#include "Renderer/RHI/GpuTextureView.h"
#include "Renderer/RHI/IEditorImGuiRHIInterop.h"

struct ImDrawData;

bool EditorImGuiVulkan_Init(const ImGuiBackendHandles& handles);
void EditorImGuiVulkan_Shutdown();
void EditorImGuiVulkan_NewFrame();
void EditorImGuiVulkan_RenderDrawData(ImDrawData* drawData, const ImGuiBackendHandles& handles);
void EditorImGuiVulkan_RenderPlatformWindows();
void* EditorImGuiVulkan_CreateTexture(const ImGuiNativeTextureInfo& info);

#endif
