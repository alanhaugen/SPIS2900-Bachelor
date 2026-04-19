#include "CommandLog.hpp"
#include <algorithm>
#include <unordered_set>

void CommandLog::Append(const Command& cmd)
{
    m_pending.push_back(cmd);
}

std::vector<Command> CommandLog::GetPending() const
{
    return m_pending;
}

void CommandLog::MarkFlushed(const std::string& idempotencyKey)
{
    auto it = std::find_if(m_pending.begin(), m_pending.end(),
        [&](const Command& c){ return c.idempotencyKey == idempotencyKey; });
    if (it != m_pending.end())
        m_pending.erase(it);
}

void CommandLog::MarkFlushedBatch(const std::vector<std::string>& keys)
{
    std::unordered_set<std::string> keySet(keys.begin(), keys.end());
    m_pending.erase(
        std::remove_if(m_pending.begin(), m_pending.end(),
            [&](const Command& c){ return keySet.count(c.idempotencyKey) > 0; }),
        m_pending.end());
}

std::size_t CommandLog::PendingCount() const
{
    return m_pending.size();
}
