#pragma once

#include "Core/Platform.h"

#if defined(MYENGINE_PLATFORM_MACOS) && defined(MYENGINE_ENABLE_IMGUI)
struct ImDrawData;

bool EditorImGuiMetal_Init(void* device);
void EditorImGuiMetal_Shutdown();
void EditorImGuiMetal_NewFrame(void* renderPassDescriptor);
void EditorImGuiMetal_RenderDrawData(ImDrawData* drawData, void* commandBuffer, void* commandEncoder);
#endif
