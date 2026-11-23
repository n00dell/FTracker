find_package(SQLite3 REQUIRED)

add_library(pesa_core STATIC
        src/paths.cpp
        src/pesa.cpp
        src/db.cpp
        src/db.h
        src/migrations.cpp
        src/migrations.h
        src/paths.cpp
        include/pesa/common.h
        include/pesa/items.h
        include/pesa/categories.h
        src/categories.cpp
        src/items.cpp
        src/util.h
        src/weekday_set.cpp
        #        ../tests/test_categories.cpp
)

# PUBLIC: anything linking pesa_core can #include <pesa/pesa.h>
target_include_directories(pesa_core PUBLIC include)
target_link_libraries(pesa_core PUBLIC SQLite3::SQLite3)
target_compile_features(pesa_core PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pesa_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
