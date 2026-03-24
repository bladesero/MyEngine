# CMake generated Testfile for 
# Source directory: /home/runner/work/MyEngine/MyEngine
# Build directory: /home/runner/work/MyEngine/MyEngine/build_linux
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[MyEngineTests]=] "/home/runner/work/MyEngine/MyEngine/build_linux/MyEngineTests")
set_tests_properties([=[MyEngineTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/MyEngine/MyEngine/CMakeLists.txt;249;add_test;/home/runner/work/MyEngine/MyEngine/CMakeLists.txt;0;")
subdirs("_deps/sdl3-build")
subdirs("_deps/nlohmann_json-build")
