local packages = myengine_package_catalog()
local imgui_configs = myengine_imgui_configs()

add_requires(packages.libsdl3.requirement, {configs = {shared = true}})
-- imgui pulls libsdl3 transitively. Force every instance to the same shared ABI.
add_requireconfs("**.libsdl3",
                 {version = "3.2.14", configs = {shared = true}, override = true})
add_requires(packages.nlohmann_json.requirement)
add_requires(packages.stb.requirement)
add_requires(packages.rmlui.requirement, {alias = packages.rmlui.alias, configs = {
    freetype = true,
    lua = false,
    svg = false,
    lottie = false,
    shared = false
}})
add_requires(packages.joltphysics.requirement, {configs = {
    shared = false,
    object_layer_bits = "32",
    object_stream = false,
    debug_renderer = true,
    cross_platform_deterministic = false
}})
add_requires(packages.angelscript.requirement)
-- Keep the package identity tied to the ImGui 1.92 compatibility patch.
add_requires(packages.imnodes.requirement, {configs = {myengine_imgui_192 = true}})

if myengine_package_enabled("vulkan-headers") then
    add_requires(packages["vulkan-headers"].requirement)
end

add_requires(packages.imgui.requirement,
             {version = packages.imgui.version, configs = imgui_configs})
add_requireconfs("imnodes.imgui",
                 {version = packages.imgui.version, configs = imgui_configs, override = true})
