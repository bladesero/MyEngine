local build_defines = {}

local function add_build_define(value)
    table.insert(build_defines, value)
end

if has_config("mem_stats") then
    add_build_define("MYENGINE_MEM_STATS")
end
if has_config("mem_tracking") then
    add_build_define("MYENGINE_MEM_TRACKING")
end
if has_config("mem_guard") then
    add_build_define("MYENGINE_MEM_GUARD")
end

local git_commit = os.getenv("MYENGINE_GIT_COMMIT") or "unknown"
add_build_define("MYENGINE_GIT_COMMIT=" .. git_commit)
add_build_define("MYENGINE_BUILD_ID=dev_0_1_0")
add_build_define("MYENGINE_BUILD_CONFIGURATION=" .. (is_mode("release") and "release" or "debug"))

if is_plat("windows") then
    add_build_define("MYENGINE_PLATFORM_WINDOWS")
    add_build_define("MYENGINE_COMPILER_MSVC")
    if myengine_enable_vulkan() then
        add_build_define("MYENGINE_ENABLE_VULKAN")
    end
elseif is_plat("macosx") then
    add_build_define("MYENGINE_PLATFORM_MACOS")
    add_build_define("MYENGINE_COMPILER_CLANG")
elseif is_plat("linux") then
    add_build_define("MYENGINE_PLATFORM_LINUX")
    add_build_define("MYENGINE_COMPILER_GCC")
end

target("MyEngine.BuildConfig")
    set_kind("headeronly")
    set_values("myengine.architecture.role", "interface")
    set_values("myengine.central_define_owner", "BuildConfig")
    set_values("myengine.central_defines", build_defines)
    add_options("mem_stats", "mem_tracking", "mem_guard", "vulkan", {public = true})
    for _, define in ipairs(build_defines) do
        add_defines(define, {public = true})
    end
target_end()

target("MyEngine.Runtime.API")
    set_kind("headeronly")
    set_values("myengine.architecture.role", "interface")
    set_values("myengine.module.public_packages", {"libsdl3", "nlohmann_json", "rmlui"})
    add_deps("MyEngine.BuildConfig")
    add_packages("libsdl3", "nlohmann_json", "rmlui", {public = true})
    add_includedirs(path.join(os.projectdir(), "src", "Runtime"), {public = true})
    add_includedirs(path.join(os.projectdir(), "src"), {public = true})
target_end()

target("MyEngine.Editor.API")
    set_kind("headeronly")
    set_values("myengine.architecture.role", "interface")
    set_values("myengine.central_define_owner", "EditorAPI")
    set_values("myengine.central_defines", {"MYENGINE_ENABLE_IMGUI"})
    add_deps("MyEngine.Runtime.API")
    add_includedirs(path.join(os.projectdir(), "src", "Editor"),
                    path.join(os.projectdir(), "thirdparty", "ImGuizmo"), {public = true})
    add_defines("MYENGINE_ENABLE_IMGUI", {public = true})
target_end()
