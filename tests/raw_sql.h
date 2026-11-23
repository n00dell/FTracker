// FTracker/tests/raw_sql.h
// Helpers that talk to a database file directly, bypassing Pesa. Used to fake
// "an older Pesa made this file" or "the daily job posted this entry".
#pragma once

#include <doctest/doctest.h>
#include <sqlite3.h>   // available because pesa_core links SQLite publicly

#include <filesystem>
#include <string>

inline void rawSql(const std::filesystem::path& file, const char* sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(file.string().c_str(), &db) == SQLITE_OK);
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    sqlite3_free(err);
    sqlite3_close(db);
    REQUIRE(rc == SQLITE_OK);
}

// Runs a query and returns the first column of the first row as text.
inline std::string firstValue(const std::filesystem::path& file, const char* sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(file.string().c_str(), &db) == SQLITE_OK);
    std::string result;
    auto onRow = [](void* out, int columns, char** values, char**) -> int {
        if (columns > 0 && values[0]) *static_cast<std::string*>(out) = values[0];
        return 0;
    };
    const int rc = sqlite3_exec(db, sql, onRow, &result, nullptr);
    sqlite3_close(db);
    REQUIRE(rc == SQLITE_OK);
    return result;
}
