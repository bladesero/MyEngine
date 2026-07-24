local excluded_dirs = {
    path.join(os.scriptdir(), "Scripting", "Bindings")
}
local private_packages = {"stb", "joltphysics", "angelscript"}
local syslinks = {}
local frameworks = {}

local runtime_backends = path.join(os.scriptdir(), "Renderer", "Backends")
if not is_plat("windows") then
    table.insert(excluded_dirs, path.join(runtime_backends, "D3DCommon"))
    table.insert(excluded_dirs, path.join(runtime_backends, "D3D11"))
    table.insert(excluded_dirs, path.join(runtime_backends, "D3D12"))
else
    table.join2(syslinks, {"d3d11", "d3d12", "dxgi", "d3dcompiler", "user32"})
end

if not (is_plat("windows") and myengine_enable_vulkan()) then
    table.insert(excluded_dirs, path.join(runtime_backends, "Vulkan"))
else
    table.insert(private_packages, "vulkan-headers")
end

if not is_plat("macosx") then
    table.insert(excluded_dirs, path.join(runtime_backends, "Metal"))
else
    table.join2(frameworks, {"Metal", "MetalKit", "QuartzCore", "AppKit"})
end

myengine_library({
    name = "MyEngineRuntime",
    domain = "runtime",
    kind = "shared",
    basename = "runtime",
    roots = {os.scriptdir()},
    excluded_dirs = excluded_dirs,
    fragment_dirs = {path.join(os.scriptdir(), "Scripting", "Bindings")},
    deps = {"MyEngine.Runtime.API", "MyEngine.ThirdParty"},
    private_packages = private_packages,
    syslinks = syslinks,
    frameworks = frameworks,
    rules = {"myengine.runtime.abi", "copy_sdl_runtime"},
    configure = function()
        if is_plat("windows") and myengine_enable_vulkan() then
            add_vulkan_loader_link()
        end
    end
})
