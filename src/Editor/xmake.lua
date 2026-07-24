local excluded_dirs = {}
local private_packages = {"imnodes", "angelscript", "libsdl3"}
local syslinks = {}
local frameworks = {}

local editor_backends = path.join(os.scriptdir(), "Backends")
if not (is_plat("windows") and myengine_enable_vulkan()) then
    table.insert(excluded_dirs, path.join(editor_backends, "Vulkan"))
else
    table.insert(private_packages, "vulkan-headers")
end

if not is_plat("macosx") then
    table.insert(excluded_dirs, path.join(editor_backends, "Metal"))
else
    table.join2(frameworks, {"Metal", "MetalKit"})
end

if is_plat("windows") then
    table.join2(syslinks, {"d3d11", "d3d12", "dxgi"})
end

myengine_library({
    name = "MyEngine.Editor",
    domain = "editor",
    kind = "static",
    roots = {os.scriptdir()},
    excluded_dirs = excluded_dirs,
    deps = {"MyEngine.Editor.API", "MyEngineRuntime", "MyEngine.ThirdParty"},
    public_packages = {"imgui", "nlohmann_json"},
    private_packages = private_packages,
    syslinks = syslinks,
    frameworks = frameworks,
    configure = function()
        if is_plat("windows") and myengine_enable_vulkan() then
            add_vulkan_loader_link()
        end
    end
})
