local catalog = {
    ["libsdl3"] = {
        requirement = "libsdl3 3.2.14",
        headers = {"SDL3/"}
    },
    ["nlohmann_json"] = {
        requirement = "nlohmann_json 3.11.3",
        headers = {"nlohmann/"}
    },
    ["stb"] = {
        requirement = "stb",
        headers = {"stb_image.h", "stb_image_write.h"}
    },
    ["rmlui"] = {
        requirement = "myengine-rmlui 6.2",
        alias = "rmlui",
        headers = {"RmlUi/"}
    },
    ["joltphysics"] = {
        requirement = "joltphysics v5.5.0",
        headers = {"Jolt/"}
    },
    ["angelscript"] = {
        requirement = "angelscript 2.38.0",
        headers = {"angelscript.h"}
    },
    ["imgui"] = {
        requirement = "imgui",
        version = "v1.92.7-docking",
        headers = {"imgui.h", "imgui_internal.h", "backends/imgui_"},
        editor_only = true
    },
    ["imnodes"] = {
        requirement = "imnodes v0.5",
        headers = {"imnodes.h"},
        editor_only = true
    },
    ["vulkan-headers"] = {
        requirement = "vulkan-headers 1.4.335+0",
        headers = {"vulkan/"},
        enabled = function()
            return myengine_enable_vulkan()
        end
    }
}

function myengine_package_catalog()
    return catalog
end

function myengine_package_enabled(name)
    local package = catalog[name]
    if not package then
        raise("unknown MyEngine package: " .. tostring(name))
    end
    return not package.enabled or package.enabled()
end

function myengine_imgui_configs()
    local configs = {sdl3 = true}
    if is_plat("windows") then
        configs.dx11 = true
        configs.dx12 = true
        configs.vulkan = myengine_enable_vulkan()
        configs.vulkan_no_proto = myengine_enable_vulkan()
    end
    return configs
end
