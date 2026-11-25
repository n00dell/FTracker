# FTracker/cli/CMakeLists.txt
# Everything except main() lives in a library, so the tests can drive it too.
add_library(pesa_cli_lib STATIC
        text.cpp
        console.cpp
        display.cpp
        pickers.cpp
        category_cmd.cpp
        item_cmd.cpp
        entry_cmd.cpp
        day_cmd.cpp
        catalog_cmd.cpp
        rule_cmd.cpp
        setup_cmd.cpp
        startup.cpp
        dispatch.cpp
        shell.cpp
        money_cmd.cpp
        log_cmd.cpp
)
target_include_directories(pesa_cli_lib PUBLIC include)
target_link_libraries(pesa_cli_lib PUBLIC pesa_core)

add_executable(pesa-cli main.cpp)

target_link_libraries(pesa-cli PRIVATE pesa_cli_lib)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pesa_cli_lib PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(pesa-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS pesa-cli RUNTIME DESTINATION bin)