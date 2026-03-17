cmake_minimum_required(VERSION 3.20)
project(MyEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/utf-8)
endif()

# --------------------------------------------------------------------------
# Dependencies – SDL3 via FetchContent
# --------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.14          # pin to a stable tag
    GIT_SHALLOW    TRUE
)

# Disable SDL tests/examples to keep build fast.
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3)

# --------------------------------------------------------------------------
# Dependencies – nlohmann/json (header-only) via FetchContent
# --------------------------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# --------------------------------------------------------------------------
# Dependencies – Dear ImGui (for editor)
# --------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(MyEngineImGui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(MyEngineImGui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(MyEngineImGui PUBLIC SDL3::SDL3 d3d11 dxgi d3dcompiler)

# --------------------------------------------------------------------------
# Runtime static library (packaged as lib for editor)
# --------------------------------------------------------------------------
add_library(MyEngineRuntime STATIC
    src/Core/Event.cpp
    src/Core/Layer.cpp
    src/Core/LayerStack.cpp
    src/Core/Time.cpp
    src/Core/Window.cpp
    src/Core/Engine.cpp
    src/Core/Application.cpp
    src/Input/Input.cpp
    src/Renderer/D3D11Context.cpp
    src/Renderer/Renderer.cpp
    src/Scene/Actor.cpp
    src/Scene/MeshRendererComponent.cpp
    src/Scene/Scene.cpp
    src/Scene/SceneSerializer.cpp
    src/Assets/MeshAsset.cpp
    src/Assets/AssetManager.cpp
    src/Camera/Camera.cpp
)

target_include_directories(MyEngineRuntime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(MyEngineRuntime PUBLIC SDL3::SDL3 d3d11 dxgi d3dcompiler nlohmann_json::nlohmann_json)

# --------------------------------------------------------------------------
# Game static library (depends on runtime)
# --------------------------------------------------------------------------
add_library(MyEngineGame STATIC
    src/Game/GameLayer.cpp
    src/Game/TriangleLayer.cpp
    src/Game/SceneLayer.cpp
    src/Game/SceneRenderLayer.cpp
)

target_include_directories(MyEngineGame PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(MyEngineGame PRIVATE MyEngineRuntime)

# --------------------------------------------------------------------------
# Editor executable
# --------------------------------------------------------------------------
add_executable(MyEngineEditor
    main.cpp
    src/Editor/EditorLayer.cpp
)

target_include_directories(MyEngineEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(MyEngineEditor PRIVATE MyEngineRuntime MyEngineImGui)
target_link_libraries(MyEngineEditor PRIVATE MyEngineGame)
target_compile_definitions(MyEngineEditor PRIVATE MYENGINE_ENABLE_IMGUI)

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
enable_testing()

add_executable(MyEngineTests
    tests/EngineTests.cpp
)

target_include_directories(MyEngineTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(MyEngineTests PRIVATE MyEngineRuntime)

add_test(NAME MyEngineTests COMMAND MyEngineTests)

# On Windows, copy SDL3.dll next to the executable automatically.
if(WIN32)
    add_custom_command(TARGET MyEngineEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:MyEngineEditor>
    )
    add_custom_command(TARGET MyEngineTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:MyEngineTests>
    )
endif()

# --------------------------------------------------------------------------
# Compiler warnings
# --------------------------------------------------------------------------
if(MSVC)
    target_compile_options(MyEngineRuntime PRIVATE /W4)
    target_compile_options(MyEngineGame    PRIVATE /W4)
    target_compile_options(MyEngineEditor  PRIVATE /W4)
    target_compile_options(MyEngineImGui   PRIVATE /W4)
    target_compile_options(MyEngineTests   PRIVATE /W4)
else()
    target_compile_options(MyEngineRuntime PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(MyEngineGame    PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(MyEngineEditor  PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(MyEngineImGui   PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(MyEngineTests   PRIVATE -Wall -Wextra -Wpedantic)
endif()
