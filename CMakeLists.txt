# FTracker/CMakeLists.txt
cmake_minimum_required(VERSION 3.21)
project(pesa LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PESA_BUILD_TESTS "Build the unit tests" ON)
option(PESA_BUILD_GUI "Build the Slint desktop app (needs the gui folder)" OFF)

add_subdirectory(core)
add_subdirectory(cli)   # the tests link the CLI library, so it is always built

if (PESA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (PESA_BUILD_GUI)
    add_subdirectory(gui)
endif ()