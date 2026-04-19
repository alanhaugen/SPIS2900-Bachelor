#include "CommandLog.hpp"
#include <algorithm>

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

std::size_t CommandLog::PendingCount() const
{
    return m_pending.size();
}
