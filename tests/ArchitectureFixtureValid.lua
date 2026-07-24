{
    manifest = {
        targets = {
            ["MyEngine.ThirdParty"] = {kind = "static", deps = {}},
            ["MyEngineRuntime"] = {
                kind = "shared",
                deps = {"MyEngine.ThirdParty"}
            },
            ["MyEngine.Editor"] = {
                kind = "static",
                deps = {"MyEngineRuntime", "MyEngine.ThirdParty"}
            }
        },
        required_libraries = {
            ["MyEngine.ThirdParty"] = "static",
            ["MyEngineRuntime"] = "shared",
            ["MyEngine.Editor"] = "static"
        },
        source_owners = {
            ["src/Runtime/Core/Core.cpp"] = {"MyEngineRuntime"},
            ["src/Editor/EditorLayer.cpp"] = {"MyEngine.Editor"}
        },
        root_script =
            "includes(\"xmake\")\nincludes(\"thirdparty\")\nincludes(\"src\")\nincludes(\"tests\")"
    }
}
