cmake_minimum_required(VERSION 3.20)

# On Apple platforms we need Objective-C++ (OBJCXX) for the Metal back-end.
if(APPLE)
    project(MyEngine LANGUAGES CXX OBJCXX)
else()
    project(MyEngine LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Objective-C++ standard (Metal / AppKit APIs require modern ObjC runtime).
if(APPLE)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

if(MSVC)
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
# Dependencies - stb_image for texture import
# --------------------------------------------------------------------------
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

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

# ImGui static library – sources and back-ends are platform-specific.
if(WIN32)
    add_library(MyEngineImGui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    )
    target_link_libraries(MyEngineImGui PUBLIC SDL3::SDL3 d3d11 d3d12 dxgi d3dcompiler)
elseif(APPLE)
    add_library(MyEngineImGui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
    )
    target_link_libraries(MyEngineImGui PUBLIC
        SDL3::SDL3
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        "-framework AppKit"
    )
else()
    add_library(MyEngineImGui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    )
    target_link_libraries(MyEngineImGui PUBLIC SDL3::SDL3)
endif()

target_include_directories(MyEngineImGui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# --------------------------------------------------------------------------
# Runtime static library (packaged as lib for editor)
# --------------------------------------------------------------------------
set(RUNTIME_SOURCES
    src/Core/Event.cpp
    src/Core/Layer.cpp
    src/Core/LayerStack.cpp
    src/Core/Time.cpp
    src/Core/Window.cpp
    src/Core/Engine.cpp
    src/Core/Application.cpp
    src/Assets/AssetManager.cpp
    src/Assets/AssetImporters.cpp
    src/Input/Input.cpp
    src/Renderer/Renderer.cpp
    src/Renderer/MainPass.cpp
    src/Renderer/ShadowPass.cpp
    src/Scene/Actor.cpp
    src/Scene/MeshRendererComponent.cpp
    src/Scene/Scene.cpp
    src/Scene/SceneSerializer.cpp
    src/Assets/MeshAsset.cpp
    src/Camera/Camera.cpp
)

if(WIN32)
    list(APPEND RUNTIME_SOURCES
        src/Renderer/D3D11Context.cpp
        src/Renderer/D3D12Context.cpp
    )
elseif(APPLE)
    list(APPEND RUNTIME_SOURCES
        src/Renderer/MetalContext.mm
    )
endif()

add_library(MyEngineRuntime STATIC ${RUNTIME_SOURCES})

target_include_directories(MyEngineRuntime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${stb_SOURCE_DIR}
)

# Platform / compiler compile-time macros.
if(WIN32)
    target_compile_definitions(MyEngineRuntime PUBLIC MYENGINE_PLATFORM_WINDOWS)
elseif(APPLE)
    target_compile_definitions(MyEngineRuntime PUBLIC MYENGINE_PLATFORM_MACOS)
elseif(UNIX)
    target_compile_definitions(MyEngineRuntime PUBLIC MYENGINE_PLATFORM_LINUX)
endif()
target_compile_definitions(MyEngineRuntime PRIVATE MYENGINE_ENABLE_IMGUI)

if(MSVC)
    target_compile_definitions(MyEngineRuntime PUBLIC MYENGINE_COMPILER_MSVC)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_definitions(MyEngineRuntime PUBLIC MYENGINE_COMPILER_CLANG)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_definitions(MyEngineRuntime PUBLIC MYENGINE_COMPILER_GCC)
endif()

# Platform link libraries.
if(WIN32)
    target_link_libraries(MyEngineRuntime PUBLIC
        SDL3::SDL3 d3d11 d3d12 dxgi d3dcompiler nlohmann_json::nlohmann_json)
elseif(APPLE)
    target_link_libraries(MyEngineRuntime PUBLIC
        SDL3::SDL3
        nlohmann_json::nlohmann_json
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        "-framework AppKit"
    )
else()
    target_link_libraries(MyEngineRuntime PUBLIC SDL3::SDL3 nlohmann_json::nlohmann_json)
endif()
target_link_libraries(MyEngineRuntime PRIVATE MyEngineImGui)

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

# On macOS, EditorLayer.cpp uses Objective-C++ (Metal + ImGui Metal backend).
if(APPLE)
    set_source_files_properties(src/Editor/EditorLayer.cpp
        PROPERTIES LANGUAGE OBJCXX)
endif()

target_include_directories(MyEngineEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(MyEngineEditor PRIVATE MyEngineRuntime MyEngineImGui)
target_link_libraries(MyEngineEditor PRIVATE MyEngineGame)
target_compile_definitions(MyEngineEditor PRIVATE MYENGINE_ENABLE_IMGUI)

# macOS bundle settings (required for proper Xcode project / app packaging).
if(APPLE)
    set_target_properties(MyEngineEditor PROPERTIES
        MACOSX_BUNDLE                TRUE
        MACOSX_BUNDLE_BUNDLE_NAME    "MyEngineEditor"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.myengine.editor"
        XCODE_ATTRIBUTE_PRODUCT_NAME "MyEngineEditor"
    )
endif()

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
