{
    expected_error = "forbidden-edge",
    manifest = {
        targets = {
            ["MyEngineRuntime"] = {kind = "shared", deps = {"MyEngine.Editor"}},
            ["MyEngine.Editor"] = {kind = "static", deps = {}}
        }
    }
}
