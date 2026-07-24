{
    expected_error = "domain-leak",
    manifest = {
        sources = {
            ["src/Runtime/Core/Engine.cpp"] = "#include \"Editor/EditorLayer.h\""
        }
    }
}
