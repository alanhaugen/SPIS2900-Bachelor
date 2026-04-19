#pragma once
#include "Command.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

// Entity stored in the in-memory database.
//
// version  – incremented on every Commit or ApplyDelta; used for OCC checks
// exists   – false after DeleteEntity; non-commutative ops check this first
// value    – generic integer payload (resource count, reputation score, etc.)
struct Entity
{
    std::string id;
    int32_t     version = 0;
    bool        exists  = true;
    int32_t     value   = 0;
};

enum class ReplayResult
{
    Committed,   // command applied; entity version incremented
    Duplicate,   // idempotencyKey already seen; command skipped silently
    Conflict,    // entity.version != cmd.expectedVersion; not applied
    Rejected,    // entity does not exist; not applied
};

// Lightweight in-memory entity store used for demo and test purposes.
// In production the online DBI connects to a SQL backend; the offline DBI
// connects to a SQLite file. This class stands in for either during testing.
class Database
{
public:
    void   AddEntity(const std::string& id,
                     int32_t initialVersion = 0,
                     int32_t initialValue   = 0);
    void   DeleteEntity(const std::string& id);
    Entity GetEntity(const std::string& id) const;

    bool   IsAlreadyApplied(const std::string& idempotencyKey) const;

    // Commutative path: adds cmd.delta to entity.value, increments version.
    ReplayResult ApplyDelta(const Command& cmd);

    // Non-commutative path: increments entity version, records idempotencyKey.
    ReplayResult Commit(const Command& cmd);

private:
    std::unordered_map<std::string, Entity> m_entities;
    std::unordered_set<std::string>         m_applied;
};
