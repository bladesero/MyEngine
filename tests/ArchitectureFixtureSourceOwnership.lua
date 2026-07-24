{
    expected_error = "source-duplicate",
    manifest = {
        source_owners = {
            ["src/Runtime/Core/Core.cpp"] = {"MyEngineRuntime", "MyEngine.Editor"},
            ["src/Runtime/Scene/Missing.cpp"] = {}
        }
    }
}
