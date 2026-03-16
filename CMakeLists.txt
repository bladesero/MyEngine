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
# Core static library
# --------------------------------------------------------------------------
add_library(MyEngineCore STATIC
    src/Core/Event.cpp
    src/Core/Layer.cpp
    src/Core/LayerStack.cpp
    src/Core/Time.cpp
    src/Core/Window.cpp
    src/Core/Engine.cpp
    src/Core/Application.cpp
    src/Input/Input.cpp
    src/Renderer/D3D11Context.cpp
    src/Scene/Actor.cpp
    src/Scene/Scene.cpp
    src/Scene/SceneSerializer.cpp
    src/Assets/MeshAsset.cpp
    src/Assets/AssetManager.cpp
)

target_include_directories(MyEngineCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(MyEngineCore PUBLIC SDL3::SDL3 d3d11 dxgi d3dcompiler nlohmann_json::nlohmann_json)

# --------------------------------------------------------------------------
# Game layer static library
# --------------------------------------------------------------------------
add_library(MyEngineGame STATIC
    src/Game/GameLayer.cpp
    src/Game/TriangleLayer.cpp
    src/Game/SceneLayer.cpp
    src/Camera/Camera.cpp
)

target_include_directories(MyEngineGame PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(MyEngineGame PRIVATE MyEngineCore)

# --------------------------------------------------------------------------
# Executable
# --------------------------------------------------------------------------
add_executable(MyEngine main.cpp)

target_include_directories(MyEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(MyEngine PRIVATE MyEngineCore MyEngineGame)

# On Windows, copy SDL3.dll next to the executable automatically.
if(WIN32)
    add_custom_command(TARGET MyEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:MyEngine>
    )
endif()

# --------------------------------------------------------------------------
# Compiler warnings
# --------------------------------------------------------------------------
if(MSVC)
    target_compile_options(MyEngineCore  PRIVATE /W4)
    target_compile_options(MyEngineGame  PRIVATE /W4)
    target_compile_options(MyEngine      PRIVATE /W4)
else()
    target_compile_options(MyEngineCore  PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(MyEngineGame  PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(MyEngine      PRIVATE -Wall -Wextra -Wpedantic)
endif()
