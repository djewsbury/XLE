cmake_minimum_required (VERSION 3.5)
cmake_policy(SET CMP0091 NEW)
project(Global CXX C)
include(Solutions/CMake/main.cmake RESULT_VARIABLE MAIN_CMAKE_LOCATION)
include(Solutions/CMake/modules.cmake)

add_subdirectory(UnitTests UnitTests)
add_subdirectory(Samples/NativeModelViewer NativeModelViewer)

