{
    expected_error = "vulkan-feature",
    manifest = {
        vulkan = {
            enabled = false,
            runtime_sources = {"src/Runtime/Renderer/Backends/Vulkan/VulkanContext.cpp"},
            editor_sources = {},
            package_targets = {}
        }
    }
}
