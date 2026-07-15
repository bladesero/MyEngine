add_requires("libsdl3 3.2.14", { configs = { shared = true } })
-- imgui pulls libsdl3 as a transitive dep; without this, that instance defaults to static and you get
-- SDL3-static.lib + SDL3.lib together (LNK2005 duplicate symbols on MSVC).
add_requireconfs("**.libsdl3", { version = "3.2.14", configs = { shared = true }, override = true })
add_requires("nlohmann_json 3.11.3")
add_requires("stb")
add_requires("myengine-rmlui 6.2", { alias = "rmlui", configs = {
    freetype = true,
    lua = false,
    svg = false,
    lottie = false,
    shared = false
} })
add_requires("tinyobjloader")
add_requires("joltphysics v5.5.0", { configs = {
    shared = false,
    object_layer_bits = "32",
    object_stream = false,
    -- xmake-repo's Jolt 5.5.0 Windows binary exposes the debug-renderer ABI.
    -- Keep the ABI define consistent; MyEngine does not instantiate a debug renderer.
    debug_renderer = true,
    cross_platform_deterministic = false
} })
add_requires("angelscript 2.38.0")
-- Keep a MyEngine compatibility stamp in the package identity. xmake package
-- install hashes do not include local recipe/patch contents, so without this a
-- pre-existing upstream imnodes v0.5 binary built against ImGui 1.91 can be
-- silently reused after the project moves to ImGui 1.92.
add_requires("imnodes v0.5", { configs = { myengine_imgui_192 = true } })

if is_plat("windows") then
    add_requires("vulkan-headers 1.4.335+0")
    add_requires("imgui", { version = "v1.92.7-docking", configs = { sdl3 = true, dx11 = true, dx12 = true, vulkan = true, vulkan_no_proto = true } })
    add_requireconfs("imnodes.imgui", { version = "v1.92.7-docking", configs = { sdl3 = true, dx11 = true, dx12 = true, vulkan = true, vulkan_no_proto = true }, override = true })
else
    add_requires("imgui", { version = "v1.92.7-docking", configs = { sdl3 = true } })
    add_requireconfs("imnodes.imgui", { version = "v1.92.7-docking", configs = { sdl3 = true }, override = true })
end

if is_plat("macosx") then
    includes("imgui_metal.lua")
end
