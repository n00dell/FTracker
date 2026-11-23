// FTracker/core/src/db.h  (PRIVATE: not visible to CLI/GUI)
// Thin RAII wrappers around the SQLite C API.
#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace pesa::detail {

// Custom "deleters": what unique_ptr calls to clean up automatically.
struct DbCloser      { void operator()(sqlite3* p)      const noexcept { sqlite3_close(p); } };
struct StmtFinalizer { void operator()(sqlite3_stmt* p) const noexcept { sqlite3_finalize(p); } };

// One prepared SQL statement. Cleaned up automatically, even if an exception flies.
class Statement {
public:
    Statement(sqlite3* db, std::string_view sql);

    // Fill in a "?" placeholder. Indexes start at 1 (SQLite's rule).
    Statement& bind(int index, std::string_view value);
    Statement& bind(int index, std::int64_t value);

    bool step();   // advance one row. true = a row is ready, false = finished
    void run();    // for INSERT/UPDATE/DELETE: execute once

    [[nodiscard]] std::string  text(int column) const;     // columns start at 0
    [[nodiscard]] std::int64_t integer(int column) const;

private:
    sqlite3* db_;   // not owned, only used to read error messages
    std::unique_ptr<sqlite3_stmt, StmtFinalizer> stmt_;
};

class Database {
public:
    explicit Database(const std::filesystem::path& file);

    void exec(const std::string& sql);            // one or more statements, no results
    [[nodiscard]] Statement prepare(std::string_view sql);
    [[nodiscard]] std::int64_t lastInsertId() const;
    [[nodiscard]] int changes() const;            // rows touched by the last write

private:
    std::unique_ptr<sqlite3, DbCloser> db_;
};

// All-or-nothing block. If commit() is never reached (an exception), it rolls back.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();

private:
    Database& db_;
    bool done_ = false;
};

} // namespace pesa::detail
