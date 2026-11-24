# FTracker/gui/CMakeLists.txt
# Slint is looked up here, not at the top level, so a CLI-only build never needs it.
find_package(Slint QUIET)          # QUIET, not REQUIRED, so the fallback below can run
if(NOT Slint_FOUND)
    message(STATUS "Slint not installed. Downloading and building it locally.")
    include(FetchContent)
    FetchContent_Declare(
            Slint
            GIT_REPOSITORY https://github.com/slint-ui/slint.git
            GIT_TAG release/1.18
            SOURCE_SUBDIR api/cpp
    )
    FetchContent_MakeAvailable(Slint)
endif()

add_executable(pesa-gui
        src/main.cpp
        src/backend/Controllers/DashController.cpp
)

target_include_directories(pesa-gui PRIVATE src)
target_link_libraries(pesa-gui PRIVATE pesa_core Slint::Slint)

slint_target_sources(pesa-gui ui/app-window.slint)

# On Windows, copy the Slint DLL next to the exe so it is found at runtime.
if(WIN32)
    add_custom_command(TARGET pesa-gui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:pesa-gui> $<TARGET_FILE_DIR:pesa-gui>
            COMMAND_EXPAND_LISTS)
endif()