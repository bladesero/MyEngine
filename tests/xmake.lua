target("MyEngineTests")
    set_kind("binary")
    add_rules("myengine.module", "copy_game_content", "copy_slang_tool", "copy_runtime_library")
    set_values("myengine.module.root", os.scriptdir())
    set_values("myengine.architecture.role", "test")
    set_values("myengine.module.private_packages", {"imgui", "imnodes", "nlohmann_json"})
    add_deps("MyEngine.Runtime.API", "MyEngine.Editor.API", "MyEngineRuntime",
             "MyEngine.Editor")
    after_build(function(target)
        local incremental_files = import("scripts.incremental_files",
                                         {rootdir = path.join(os.projectdir(), "xmake")})
        incremental_files.sync_managed_directories({
            {
                source = path.join(os.projectdir(), "tests", "fixtures"),
                destination = path.join("tests", "fixtures")
            }
        }, target:targetdir(),
          path.join(target:targetdir(), ".myengine", "tests-fixtures.manifest"))
    end)
target_end()
