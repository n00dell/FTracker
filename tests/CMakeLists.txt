# FTracker/tests/CMakeLists.txt
include(FetchContent)
FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.12)
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

add_executable(pesa_tests
        main.cpp
        test_weekdays.cpp
        test_date.cpp
        test_categories.cpp
        test_items.cpp
        test_entries.cpp
        test_migrations.cpp
        test_cli_text.cpp
        test_cli_commands.cpp
        test_settings.cpp
        test_rules.cpp
        test_daily_job.cpp
        test_periods.cpp
        test_savings.cpp
        test_log.cpp
        test_corrections.cpp
        test_goals.cpp
)
target_link_libraries(pesa_tests PRIVATE pesa_core pesa_cli_lib doctest::doctest)
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(pesa_tests)