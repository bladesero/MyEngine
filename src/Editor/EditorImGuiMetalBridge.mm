#include "Editor/EditorImGuiMetalBridge.h"

#if defined(MYENGINE_PLATFORM_MACOS) && defined(MYENGINE_ENABLE_IMGUI)

#import <Metal/Metal.h>

#include <backends/imgui_impl_metal.h>

bool EditorImGuiMetal_Init(void* device) {
    return ImGui_ImplMetal_Init((__bridge id<MTLDevice>)device);
}

void EditorImGuiMetal_Shutdown() {
    ImGui_ImplMetal_Shutdown();
}

void EditorImGuiMetal_NewFrame(void* renderPassDescriptor) {
    ImGui_ImplMetal_NewFrame((__bridge MTLRenderPassDescriptor*)renderPassDescriptor);
}

void EditorImGuiMetal_RenderDrawData(ImDrawData* drawData, void* commandBuffer, void* commandEncoder) {
    ImGui_ImplMetal_RenderDrawData(drawData, (__bridge id<MTLCommandBuffer>)commandBuffer,
                                   (__bridge id<MTLRenderCommandEncoder>)commandEncoder);
}

#endif
