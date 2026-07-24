{
    expected_error = "backend-leak",
    manifest = {
        sources = {
            ["src/Runtime/Renderer/Renderer.cpp"] = "#include <d3d12.h>"
        }
    }
}
