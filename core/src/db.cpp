// FTracker/core/src/db.cpp
#include "db.h"

#include <pesa/common.h>

namespace pesa::detail {

// ---------- Statement ----------

Statement::Statement(sqlite3* db, std::string_view sql) : db_(db) {
    sqlite3_stmt* raw = nullptr;
    const int rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &raw, nullptr);
    stmt_.reset(raw);   // hand ownership to the smart pointer immediately
    if (rc != SQLITE_OK) throw Error(sqlite3_errmsg(db));
}

Statement& Statement::bind(int index, std::string_view value) {
    // SQLITE_TRANSIENT = "SQLite, make your own copy of this text".
    const int rc = sqlite3_bind_text(stmt_.get(), index, value.data(),
                                     static_cast<int>(value.size()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) throw Error(sqlite3_errmsg(db_));
    return *this;
}

Statement& Statement::bind(int index, std::int64_t value) {
    if (sqlite3_bind_int64(stmt_.get(), index, value) != SQLITE_OK) throw Error(sqlite3_errmsg(db_));
    return *this;
}

Statement& Statement::bindNull(int index) {
    if (sqlite3_bind_null(stmt_.get(), index) != SQLITE_OK) throw Error(sqlite3_errmsg(db_));
    return *this;
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_.get());
    if (rc == SQLITE_ROW)  return true;
    if (rc == SQLITE_DONE) return false;
    throw Error(sqlite3_errmsg(db_));
}

void Statement::run() { step(); }

std::string Statement::text(int column) const {
    const unsigned char* t = sqlite3_column_text(stmt_.get(), column);
    return t ? reinterpret_cast<const char*>(t) : std::string{};
}

std::int64_t Statement::integer(int column) const {
    return sqlite3_column_int64(stmt_.get(), column);
}

bool Statement::isNull(int column) const {
    return sqlite3_column_type(stmt_.get(), column) == SQLITE_NULL;
}

// ---------- Database ----------

Database::Database(const std::filesystem::path& file) {
    // Make the folder if the path has one (":memory:" has none, which is fine).
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path());
    }

    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(file.string().c_str(), &raw,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    db_.reset(raw);   // even a failed open may give a handle that must be closed
    if (rc != SQLITE_OK) {
        throw Error(std::string("Could not open database: ") +
                    (raw ? sqlite3_errmsg(raw) : "out of memory"));
    }

    sqlite3_busy_timeout(db_.get(), 3000);   // wait up to 3s if CLI/GUI collide
    exec("PRAGMA foreign_keys = ON;");       // OFF by default in SQLite, per connection!
    exec("PRAGMA journal_mode = WAL;");
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_.get(), sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown sqlite error";
        sqlite3_free(err);
        throw Error(msg);
    }
}

Statement Database::prepare(std::string_view sql) { return Statement(db_.get(), sql); }

std::int64_t Database::lastInsertId() const { return sqlite3_last_insert_rowid(db_.get()); }
int Database::changes() const { return sqlite3_changes(db_.get()); }
bool Database::inTransaction() const { return sqlite3_get_autocommit(db_.get()) == 0; }

// ---------- Transaction ----------

Transaction::Transaction(Database& db) : db_(db), owner_(!db.inTransaction()) {
    if (owner_) db_.exec("BEGIN IMMEDIATE;");
}

Transaction::~Transaction() {
    if (owner_ && !done_) {
        try { db_.exec("ROLLBACK;"); } catch (...) {}   // destructors must never throw
    }
}

void Transaction::commit() {
    if (owner_) db_.exec("COMMIT;");
    done_ = true;
}

} // namespace pesa::detail
