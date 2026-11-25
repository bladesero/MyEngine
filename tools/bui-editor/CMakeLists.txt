cmake_minimum_required(VERSION 3.20)
project(MyEngineBuiEditor LANGUAGES C CXX)
include(FetchContent)

# Native kit, not BUI's wrapper around Dear ImGui. Keep this target independent
# of both the legacy editor and the engine's renderer/SDL event loop.
set(BUI_BUILD_IMGUI OFF CACHE BOOL "" FORCE)
set(BUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUI_BUILD_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
set(BUI_BUILD_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(BUI_BUILD_QT_BACKEND OFF CACHE BOOL "" FORCE)
foreach(feature IN ITEMS VIDEO IMAGE_JPEG IMAGE_WEBP IMAGE_PNG IMAGE_GIF IMAGE_BMP
    IMAGE_TGA IMAGE_QOI IMAGE_HDR IMAGE_PNM FILEBROWSER ANIM)
    set(BUI_${feature} OFF CACHE BOOL "" FORCE)
endforeach()

FetchContent_Declare(bui
    GIT_REPOSITORY https://github.com/ppmpreetham/BUI.git
    GIT_TAG d59c280706e841040cac8e3e042fda9187ac3238)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 55f93686c01528224f448c19128836e7df245f72)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(bui nlohmann_json)

add_executable(MyEngineBuiEditor BuiEditorMain.cpp)
target_compile_features(MyEngineBuiEditor PRIVATE cxx_std_17)
target_link_libraries(MyEngineBuiEditor PRIVATE bui::kit nlohmann_json::nlohmann_json)

# Loadable native BUI skin for the engine-owned SDL/RHI viewport. The kit
# never opens a native window or calls any platform presentation function.
set(viewport_headers "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Apps/BuiViewport")
add_library(MyEngineBuiViewportKit SHARED BuiViewportKit.cpp)
target_compile_features(MyEngineBuiViewportKit PRIVATE cxx_std_17)
target_include_directories(MyEngineBuiViewportKit PRIVATE "${viewport_headers}")
target_link_libraries(MyEngineBuiViewportKit PRIVATE bui::kit)
set_target_properties(MyEngineBuiViewportKit PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
if(APPLE)
    target_link_options(MyEngineBuiViewportKit PRIVATE "LINKER:-exported_symbol,_MyEngineBuiGetApi")
elseif(UNIX)
    target_link_options(MyEngineBuiViewportKit PRIVATE "LINKER:--exclude-libs,ALL")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(MyEngineBuiViewportContractTest BuiViewportContractTest.cpp)
    target_compile_features(MyEngineBuiViewportContractTest PRIVATE cxx_std_17)
    target_include_directories(MyEngineBuiViewportContractTest PRIVATE "${viewport_headers}")
    target_link_libraries(MyEngineBuiViewportContractTest PRIVATE MyEngineBuiViewportKit)
    add_test(NAME MyEngineBuiViewportContracts COMMAND MyEngineBuiViewportContractTest)
    add_test(NAME MyEngineBuiProjectSmoke COMMAND MyEngineBuiEditor
        --self-test --project "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    add_test(NAME MyEngineBuiInvalidProject COMMAND MyEngineBuiEditor
        --self-test --project "${CMAKE_CURRENT_BINARY_DIR}/no-such-project")
    set_tests_properties(MyEngineBuiInvalidProject PROPERTIES WILL_FAIL TRUE)
    add_test(NAME MyEngineBuiMalformedScene COMMAND "${CMAKE_COMMAND}"
        "-Deditor=$<TARGET_FILE:MyEngineBuiEditor>"
        "-Dfixture=${CMAKE_CURRENT_BINARY_DIR}/malformed-scene-fixture"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/MalformedSceneTest.cmake")
endif()
