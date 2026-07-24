target("MyEngineCooker")
    set_kind("binary")
    add_rules("myengine.module", "myengine.app_icons", "copy_slang_tool",
              "copy_runtime_library")
    set_values("myengine.module.root", os.scriptdir())
    set_values("myengine.architecture.role", "app")
    add_deps("MyEngine.Runtime.API", "MyEngine.Editor.API", "MyEngineRuntime",
             "MyEngine.Editor", "MyEngineIconTool")
    set_rundir("$(projectdir)")
target_end()
