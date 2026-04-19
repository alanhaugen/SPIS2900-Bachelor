#pragma once
#include "IDatabaseInterface.hpp"
#include <sqlite3.h>
#include <string>

// SQLite-backed implementation of IDatabaseInterface for offline sessions.
//
// Maintains the same game-domain tables a cloud SQL server would hold, but
// in a local SQLite file so gameplay can continue without connectivity.
// When the player reconnects, the CommandLog is replayed against the
// authoritative server and this local state can be discarded or replaced.
//
// Schema
// ──────
//   buildings       – player-owned placed structures
//   item_inventory  – (player_id, item_id) → quantity
//   completed_quests– (player_id, quest_id) pairs
//   faction_relations–(faction_a, faction_b) → signed relation score
//
// All mutating methods execute inside an explicit SQLite transaction and
// return false (rolling back) if any step fails, matching the atomicity
// contract of the online DBI.
class SQLiteDBI : public IDatabaseInterface
{
public:
    // dbPath: file path for the SQLite database, or ":memory:" for tests.
    explicit SQLiteDBI(const std::string& dbPath);
    ~SQLiteDBI() override;

    SQLiteDBI(const SQLiteDBI&)            = delete;
    SQLiteDBI& operator=(const SQLiteDBI&) = delete;

    bool CreateBuilding(int32_t playerId, int32_t buildingTypeId,
                        float posX, float posY, float posZ) override;
    bool DestroyBuilding(int32_t buildingId) override;

    // Deducts quantity from fromPlayerId and credits toPlayerId atomically.
    // Returns false if fromPlayerId has insufficient quantity.
    bool TransferItem(int32_t fromPlayerId, int32_t toPlayerId,
                      int32_t itemId, int32_t quantity) override;

    // Idempotent: completing an already-completed quest succeeds silently.
    bool CompleteQuest(int32_t playerId, int32_t questId) override;

    // Adds relationDelta to the existing score (UPSERT).
    bool UpdateFactionRelation(int32_t factionA, int32_t factionB,
                               int32_t relationDelta) override;

    // ── Read-back helpers for tests and game UI ───────────────────────────────
    int32_t GetItemQuantity(int32_t playerId, int32_t itemId)      const;
    bool    IsQuestCompleted(int32_t playerId, int32_t questId)    const;
    int32_t GetFactionRelation(int32_t factionA, int32_t factionB) const;
    bool    BuildingExists(int32_t buildingId)                     const;

private:
    void InitSchema();

    sqlite3* m_db = nullptr;
};
