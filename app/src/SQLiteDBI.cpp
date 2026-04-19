#include "SQLiteDBI.hpp"
#include <stdexcept>

// ── RAII wrapper for sqlite3_stmt ─────────────────────────────────────────────
namespace {

struct Stmt
{
    sqlite3_stmt* ptr = nullptr;
    ~Stmt() { if (ptr) sqlite3_finalize(ptr); }
    sqlite3_stmt** operator&() { return &ptr; }
    operator sqlite3_stmt*()   { return ptr;  }
};

bool Step(sqlite3_stmt* stmt)
{
    int rc = sqlite3_step(stmt);
    return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

} // namespace

// ── Construction / destruction ────────────────────────────────────────────────

SQLiteDBI::SQLiteDBI(const std::string& dbPath)
{
    if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK)
        throw std::runtime_error(std::string("SQLiteDBI: cannot open database: ")
                                 + sqlite3_errmsg(m_db));
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA foreign_keys=ON;",  nullptr, nullptr, nullptr);
    InitSchema();
}

SQLiteDBI::~SQLiteDBI()
{
    if (m_db) sqlite3_close(m_db);
}

void SQLiteDBI::InitSchema()
{
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS buildings (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            player_id INTEGER NOT NULL,
            type_id   INTEGER NOT NULL,
            pos_x     REAL    NOT NULL,
            pos_y     REAL    NOT NULL,
            pos_z     REAL    NOT NULL
        );

        CREATE TABLE IF NOT EXISTS item_inventory (
            player_id INTEGER NOT NULL,
            item_id   INTEGER NOT NULL,
            quantity  INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (player_id, item_id)
        );

        CREATE TABLE IF NOT EXISTS completed_quests (
            player_id INTEGER NOT NULL,
            quest_id  INTEGER NOT NULL,
            PRIMARY KEY (player_id, quest_id)
        );

        CREATE TABLE IF NOT EXISTS faction_relations (
            faction_a INTEGER NOT NULL,
            faction_b INTEGER NOT NULL,
            relation  INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (faction_a, faction_b)
        );
    )";

    char* errMsg = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK)
    {
        std::string msg = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        throw std::runtime_error("SQLiteDBI::InitSchema failed: " + msg);
    }
}

// ── IDatabaseInterface implementation ────────────────────────────────────────

bool SQLiteDBI::CreateBuilding(int32_t playerId, int32_t buildingTypeId,
                               float posX, float posY, float posZ)
{
    const char* sql =
        "INSERT INTO buildings (player_id, type_id, pos_x, pos_y, pos_z) "
        "VALUES (?, ?, ?, ?, ?);";

    Stmt stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt,  1, playerId);
    sqlite3_bind_int(stmt,  2, buildingTypeId);
    sqlite3_bind_double(stmt, 3, posX);
    sqlite3_bind_double(stmt, 4, posY);
    sqlite3_bind_double(stmt, 5, posZ);

    return Step(stmt);
}

bool SQLiteDBI::DestroyBuilding(int32_t buildingId)
{
    const char* sql = "DELETE FROM buildings WHERE id = ?;";

    Stmt stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt, 1, buildingId);
    if (!Step(stmt))
        return false;

    return sqlite3_changes(m_db) > 0;
}

bool SQLiteDBI::TransferItem(int32_t fromPlayerId, int32_t toPlayerId,
                             int32_t itemId, int32_t quantity)
{
    sqlite3_exec(m_db, "BEGIN;", nullptr, nullptr, nullptr);

    // Check sender has enough stock
    const char* checkSql =
        "SELECT quantity FROM item_inventory "
        "WHERE player_id = ? AND item_id = ?;";

    Stmt checkStmt;
    if (sqlite3_prepare_v2(m_db, checkSql, -1, &checkStmt, nullptr) != SQLITE_OK)
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_int(checkStmt, 1, fromPlayerId);
    sqlite3_bind_int(checkStmt, 2, itemId);

    int32_t held = 0;
    if (sqlite3_step(checkStmt) == SQLITE_ROW)
        held = sqlite3_column_int(checkStmt, 0);

    if (held < quantity)
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    // Deduct from sender
    const char* deductSql =
        "UPDATE item_inventory SET quantity = quantity - ? "
        "WHERE player_id = ? AND item_id = ?;";

    Stmt deductStmt;
    if (sqlite3_prepare_v2(m_db, deductSql, -1, &deductStmt, nullptr) != SQLITE_OK)
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_int(deductStmt, 1, quantity);
    sqlite3_bind_int(deductStmt, 2, fromPlayerId);
    sqlite3_bind_int(deductStmt, 3, itemId);
    if (!Step(deductStmt))
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    // Credit recipient (UPSERT)
    const char* creditSql =
        "INSERT INTO item_inventory (player_id, item_id, quantity) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(player_id, item_id) "
        "DO UPDATE SET quantity = quantity + excluded.quantity;";

    Stmt creditStmt;
    if (sqlite3_prepare_v2(m_db, creditSql, -1, &creditStmt, nullptr) != SQLITE_OK)
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    sqlite3_bind_int(creditStmt, 1, toPlayerId);
    sqlite3_bind_int(creditStmt, 2, itemId);
    sqlite3_bind_int(creditStmt, 3, quantity);
    if (!Step(creditStmt))
    {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool SQLiteDBI::CompleteQuest(int32_t playerId, int32_t questId)
{
    // INSERT OR IGNORE makes this idempotent: completing an already-completed
    // quest is accepted silently, matching the server-side behaviour.
    const char* sql =
        "INSERT OR IGNORE INTO completed_quests (player_id, quest_id) "
        "VALUES (?, ?);";

    Stmt stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt, 1, playerId);
    sqlite3_bind_int(stmt, 2, questId);
    return Step(stmt);
}

bool SQLiteDBI::UpdateFactionRelation(int32_t factionA, int32_t factionB,
                                      int32_t relationDelta)
{
    // UPSERT: insert with delta as the initial value, or add delta to existing.
    const char* sql =
        "INSERT INTO faction_relations (faction_a, faction_b, relation) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(faction_a, faction_b) "
        "DO UPDATE SET relation = relation + excluded.relation;";

    Stmt stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt, 1, factionA);
    sqlite3_bind_int(stmt, 2, factionB);
    sqlite3_bind_int(stmt, 3, relationDelta);
    return Step(stmt);
}

// ── Read-back helpers ─────────────────────────────────────────────────────────

int32_t SQLiteDBI::GetItemQuantity(int32_t playerId, int32_t itemId) const
{
    const char* sql =
        "SELECT quantity FROM item_inventory "
        "WHERE player_id = ? AND item_id = ?;";

    Stmt stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;

    sqlite3_bind_int(stmt, 1, playerId);
    sqlite3_bind_int(stmt, 2, itemId);
    return (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : 0;
}

bool SQLiteDBI::IsQuestCompleted(int32_t playerId, int32_t questId) const
{
    const char* sql =
        "SELECT 1 FROM completed_quests "
        "WHERE player_id = ? AND quest_id = ?;";

    Stmt stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt, 1, playerId);
    sqlite3_bind_int(stmt, 2, questId);
    return sqlite3_step(stmt) == SQLITE_ROW;
}

int32_t SQLiteDBI::GetFactionRelation(int32_t factionA, int32_t factionB) const
{
    const char* sql =
        "SELECT relation FROM faction_relations "
        "WHERE faction_a = ? AND faction_b = ?;";

    Stmt stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;

    sqlite3_bind_int(stmt, 1, factionA);
    sqlite3_bind_int(stmt, 2, factionB);
    return (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : 0;
}

bool SQLiteDBI::BuildingExists(int32_t buildingId) const
{
    const char* sql = "SELECT 1 FROM buildings WHERE id = ?;";

    Stmt stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt, 1, buildingId);
    return sqlite3_step(stmt) == SQLITE_ROW;
}
