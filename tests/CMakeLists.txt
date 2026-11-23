
include(FetchContent)
FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.12)
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

add_executable(pesa_tests test_categories.cpp
        temp_db.h
        test_items.cpp
        test_migrations.cpp
        test_weekdays.cpp
        main.cpp
        raw_sql.h
        test_date.cpp
        test_entries.cpp)
target_link_libraries(pesa_tests PRIVATE pesa_core doctest::doctest)
add_test(NAME pesa_tests COMMAND pesa_tests)