cmake_minimum_required(VERSION 3.20)
project(MyEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --------------------------------------------------------------------------
# Core static library
# --------------------------------------------------------------------------
add_library(MyEngineCore STATIC
    src/Core/Event.cpp
    src/Core/Layer.cpp
    src/Core/LayerStack.cpp
    src/Core/Time.cpp
    src/Core/Engine.cpp
)

target_include_directories(MyEngineCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# --------------------------------------------------------------------------
# Game layer static library
# --------------------------------------------------------------------------
add_library(MyEngineGame STATIC
    src/Game/GameLayer.cpp
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

# --------------------------------------------------------------------------
# Compiler warnings
# --------------------------------------------------------------------------
if(MSVC)
    target_compile_options(MyEngineCore  PRIVATE /W4 /WX)
    target_compile_options(MyEngineGame  PRIVATE /W4 /WX)
    target_compile_options(MyEngine      PRIVATE /W4 /WX)
else()
    target_compile_options(MyEngineCore  PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(MyEngineGame  PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(MyEngine      PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
