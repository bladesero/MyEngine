local lua_defines = {}
local lua_syslinks = {}
if is_plat("linux") then
    table.insert(lua_defines, "LUA_USE_LINUX")
    table.insert(lua_syslinks, "dl")
    table.insert(lua_syslinks, "m")
elseif is_plat("macosx") then
    table.insert(lua_defines, "LUA_USE_MACOSX")
end

myengine_library({
    name = "MyEngine.ThirdParty",
    domain = "thirdparty",
    kind = "static",
    roots = {
        path.join(os.scriptdir(), "lua"),
        path.join(os.scriptdir(), "ImGuizmo"),
        path.join(os.scriptdir(), "angelscript_addons", "scriptstdstring")
    },
    deps = {"MyEngine.BuildConfig"},
    languages = {"c11", "c++17"},
    excluded_files = {
        path.join(os.scriptdir(), "lua", "lua.c"),
        path.join(os.scriptdir(), "lua", "luac.c")
    },
    public_includedirs = {
        os.scriptdir(),
        path.join(os.scriptdir(), "lua"),
        path.join(os.scriptdir(), "ImGuizmo"),
        path.join(os.scriptdir(), "angelscript_addons", "scriptstdstring"),
        path.join(os.scriptdir(), "miniaudio")
    },
    private_packages = {"imgui", "angelscript"},
    defines = lua_defines,
    syslinks = lua_syslinks,
    warnings = "none"
})
