{
    expected_error = "fragment-multiple",
    manifest = {
        fragments = {
            ["Bindings/Generated.cpp"] = {
                owner = "MyEngineRuntime",
                includes = {
                    {owner = "MyEngineRuntime"},
                    {owner = "MyEngineRuntime"}
                }
            }
        }
    }
}
