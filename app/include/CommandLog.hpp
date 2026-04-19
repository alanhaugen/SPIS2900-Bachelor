#pragma once
#include "Command.hpp"
#include <string>
#include <vector>

// Durable ordered log of commands generated during an offline session.
//
// Append       – records a new command as pending
// GetPending   – returns all commands not yet flushed, in insertion order
// MarkFlushed  – removes one entry by idempotencyKey after confirmed replay
// PendingCount – convenience accessor
class CommandLog
{
public:
    void                 Append(const Command& cmd);
    std::vector<Command> GetPending() const;
    void                 MarkFlushed(const std::string& idempotencyKey);
    void                 MarkFlushedBatch(const std::vector<std::string>& keys);
    std::size_t          PendingCount() const;

private:
    std::vector<Command> m_pending;
};
