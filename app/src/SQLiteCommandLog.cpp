#include "SQLiteCommandLog.hpp"
#include <stdexcept>

namespace {

struct Stmt
{
    sqlite3_stmt* ptr = nullptr;
    ~Stmt() { if (ptr) sqlite3_finalize(ptr); }
    sqlite3_stmt** operator&() { return &ptr; }
    operator sqlite3_stmt*()   { return ptr;  }
};

} // namespace

SQLiteCommandLog::SQLiteCommandLog(const std::string& dbPath)
{
    if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK)
        throw std::runtime_error(std::string("SQLiteCommandLog: cannot open database: ")
                                 + sqlite3_errmsg(m_db));
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    InitSchema();
}

SQLiteCommandLog::~SQLiteCommandLog()
{
    if (m_db) sqlite3_close(m_db);
}

void SQLiteCommandLog::InitSchema()
{
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS command_log (
            rowid            INTEGER PRIMARY KEY AUTOINCREMENT,
            type             TEXT    NOT NULL,
            timestamp        INTEGER NOT NULL,
            idempotency_key  TEXT    NOT NULL UNIQUE,
            entity_id        TEXT    NOT NULL,
            expected_version INTEGER NOT NULL,
            delta            INTEGER NOT NULL
        );
    )";

    char* errMsg = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK)
    {
        std::string msg = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        throw std::runtime_error("SQLiteCommandLog::InitSchema failed: " + msg);
    }
}

void SQLiteCommandLog::Append(const Command& cmd)
{
    const char* sql =
        "INSERT OR IGNORE INTO command_log "
        "(type, timestamp, idempotency_key, entity_id, expected_version, delta) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    Stmt stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return;

    sqlite3_bind_text(stmt, 1, cmd.type.c_str(),           -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, cmd.timestamp);
    sqlite3_bind_text(stmt, 3, cmd.idempotencyKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, cmd.entityId.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,  5, cmd.expectedVersion);
    sqlite3_bind_int(stmt,  6, cmd.delta);
    sqlite3_step(stmt);
}

std::vector<Command> SQLiteCommandLog::GetPending() const
{
    const char* sql =
        "SELECT type, timestamp, idempotency_key, entity_id, expected_version, delta "
        "FROM command_log ORDER BY rowid ASC;";

    Stmt stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return {};

    std::vector<Command> result;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        Command c;
        c.type            = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        c.timestamp       = sqlite3_column_int64(stmt, 1);
        c.idempotencyKey  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        c.entityId        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        c.expectedVersion = sqlite3_column_int(stmt, 4);
        c.delta           = sqlite3_column_int(stmt, 5);
        result.push_back(std::move(c));
    }
    return result;
}

void SQLiteCommandLog::MarkFlushed(const std::string& idempotencyKey)
{
    const char* sql = "DELETE FROM command_log WHERE idempotency_key = ?;";

    Stmt stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return;

    sqlite3_bind_text(stmt, 1, idempotencyKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
}

std::size_t SQLiteCommandLog::PendingCount() const
{
    const char* sql = "SELECT COUNT(*) FROM command_log;";

    Stmt stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;

    return (sqlite3_step(stmt) == SQLITE_ROW)
        ? static_cast<std::size_t>(sqlite3_column_int64(stmt, 0))
        : 0;
}
