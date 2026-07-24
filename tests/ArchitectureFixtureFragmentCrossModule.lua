{
    expected_error = "fragment-cross-library",
    manifest = {
        fragments = {
            ["Bindings/Generated.cpp"] = {
                owner = "MyEngineRuntime",
                includes = {{owner = "MyEngine.Editor"}}
            }
        }
    }
}
