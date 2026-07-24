{
    expected_error = "forbidden-edge",
    manifest = {
        targets = {
            ["MyEngine.ThirdParty"] = {deps = {"MyEngineRuntime"}},
            ["MyEngineRuntime"] = {deps = {}}
        }
    }
}
