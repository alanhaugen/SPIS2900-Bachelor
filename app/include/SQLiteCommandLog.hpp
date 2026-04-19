#pragma once
#include "Command.hpp"
#include <sqlite3.h>
#include <string>
#include <vector>

// Durable command log backed by SQLite.
//
// Drop-in replacement for the in-memory CommandLog for production offline
// sessions. Pending commands survive process crashes and device restarts
// because they are written to a SQLite file before the method returns.
//
// Ordering is preserved by an autoincrement rowid; GetPending returns
// commands in insertion order. MarkFlushed removes a single entry by
// idempotency key so the caller controls when a command is considered done.
class SQLiteCommandLog
{
public:
    explicit SQLiteCommandLog(const std::string& dbPath);
    ~SQLiteCommandLog();

    SQLiteCommandLog(const SQLiteCommandLog&)            = delete;
    SQLiteCommandLog& operator=(const SQLiteCommandLog&) = delete;

    void                 Append(const Command& cmd);
    std::vector<Command> GetPending() const;
    void                 MarkFlushed(const std::string& idempotencyKey);
    std::size_t          PendingCount() const;

private:
    void InitSchema();

    sqlite3* m_db = nullptr;
};
