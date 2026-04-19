#include "Database.hpp"

void Database::AddEntity(const std::string& id,
                         int32_t initialVersion,
                         int32_t initialValue)
{
    m_entities[id] = Entity{id, initialVersion, true, initialValue};
}

void Database::DeleteEntity(const std::string& id)
{
    auto it = m_entities.find(id);
    if (it != m_entities.end())
        it->second.exists = false;
}

Entity Database::GetEntity(const std::string& id) const
{
    auto it = m_entities.find(id);
    if (it == m_entities.end())
        return Entity{id, 0, false, 0};
    return it->second;
}

bool Database::IsAlreadyApplied(const std::string& idempotencyKey) const
{
    return m_applied.count(idempotencyKey) > 0;
}

ReplayResult Database::ApplyDelta(const Command& cmd)
{
    auto it = m_entities.find(cmd.entityId);
    if (it == m_entities.end())
    {
        // Auto-create for commutative ops so resource/reputation gains are
        // never silently dropped when the entity is new on the server.
        AddEntity(cmd.entityId, 0, 0);
        it = m_entities.find(cmd.entityId);
    }
    it->second.value   += cmd.delta;
    it->second.version += 1;
    m_applied.insert(cmd.idempotencyKey);
    return ReplayResult::Committed;
}

ReplayResult Database::Commit(const Command& cmd)
{
    auto it = m_entities.find(cmd.entityId);
    if (it == m_entities.end())
        return ReplayResult::Rejected;

    it->second.version += 1;
    m_applied.insert(cmd.idempotencyKey);
    return ReplayResult::Committed;
}
