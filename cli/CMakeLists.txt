add_executable(pesa-cli main.cpp
        category_cmd.cpp
        cli.h
        item_cmd.cpp
        util.cpp
        day_cmd.cpp
        entry_cmd.cpp)
target_link_libraries(pesa-cli PRIVATE pesa_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pesa-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS pesa-cli RUNTIME DESTINATION bin)