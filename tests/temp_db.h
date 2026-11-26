// FTracker/tests/temp_db.h
// A database file in the temp folder that deletes itself when the test ends (RAII again).
#pragma once

#include <filesystem>
#include <string>

struct TempDb {
    std::filesystem::path path;

    explicit TempDb(const std::string &name)
        : path(std::filesystem::temp_directory_path() / name) { cleanup(); }

    ~TempDb() { cleanup(); }

    TempDb(const TempDb &) = delete;

    TempDb &operator=(const TempDb &) = delete;

private:
    void cleanup() const {
        std::error_code ec; // the no-throw version: it is fine if a file is missing
        for (const char *suffix: {"", "-wal", "-shm"}) {
            std::filesystem::remove(path.string() + suffix, ec);
        }
    }
};
