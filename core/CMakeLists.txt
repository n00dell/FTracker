# FTracker/core/CMakeLists.txt
find_package(SQLite3 REQUIRED)

add_library(pesa_core STATIC
        src/paths.cpp
        src/pesa.cpp
        src/db.cpp
        src/db.h
        src/migrations.cpp
        src/migrations.h
        src/categories.cpp
        src/items.cpp
        src/util.h
        src/weekday_set.cpp
        src/date.cpp
        src/days.cpp
        src/entries.cpp
        src/entry_sql.cpp
        src/entry_sql.h
        src/rules.cpp
        src/settings.cpp
        include/pesa/common.h
        include/pesa/items.h
        include/pesa/categories.h
        include/pesa/date.h
        include/pesa/days.h
        include/pesa/entries.h
        include/pesa/rules.h
        include/pesa/settings.h
        include/pesa/daily_job.h
        src/daily_job.cpp
        src/money_sql.h
        src/money_sql.cpp
        include/pesa/savings.h
        src/savings.cpp
        include/pesa/periods.h
        src/periods.cpp
        src/format.cpp
        include/pesa/log.h
        src/log.cpp
        src/log_sql.h
        src/log_sql.cpp
        include/pesa/corrections.h
        src/corrections_sql.cpp
        src/corrections_sql.h
        include/pesa/skips.h
        src/skips.cpp
        include/pesa/restore_points.h
        src/restore_points.cpp
)

# PUBLIC: anything linking pesa_core can #include <pesa/pesa.h>
target_include_directories(pesa_core PUBLIC include)
target_link_libraries(pesa_core PUBLIC SQLite3::SQLite3)
target_compile_features(pesa_core PUBLIC cxx_std_20)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pesa_core PRIVATE -Wall -Wextra -Wpedantic)
endif ()