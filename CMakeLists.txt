cmake_minimum_required (VERSION 3.3)
project(Global CXX C)
include(Solutions/CMake/main.cmake RESULT_VARIABLE MAIN_CMAKE_LOCATION)
include(Solutions/CMake/modules.cmake)

add_subdirectory(UnitTests UnitTests)

