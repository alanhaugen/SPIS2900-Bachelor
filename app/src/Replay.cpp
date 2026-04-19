#include "Replay.hpp"
#include <unordered_set>

static const std::unordered_set<std::string> kCommutativeTypes = {
    "ResourceDelta",
    "ReputationDelta",
};

ReplayResult ReplayCommand(const Command& cmd, Database& db,
                           RejectionManifest& manifest)
{
    // Step 1: idempotency — skip commands the server has already applied
    if (db.IsAlreadyApplied(cmd.idempotencyKey))
        return ReplayResult::Duplicate;

    // Step 2: commutative operations — apply regardless of entity version
    if (kCommutativeTypes.count(cmd.type))
        return db.ApplyDelta(cmd);

    // Steps 3–4: non-commutative — validate entity state before committing
    Entity entity = db.GetEntity(cmd.entityId);

    if (!entity.exists)
    {
        manifest.push_back({cmd, ReplayResult::Rejected});
        return ReplayResult::Rejected;
    }

    if (entity.version != cmd.expectedVersion)
    {
        manifest.push_back({cmd, ReplayResult::Conflict});
        return ReplayResult::Conflict;
    }

    // Step 5: all checks passed — commit
    return db.Commit(cmd);
}

RejectionManifest ReplayAll(CommandLog& log, Database& db)
{
    RejectionManifest manifest;

    // Snapshot pending list so MarkFlushed calls don't invalidate iteration.
    std::vector<Command> pending = log.GetPending();

    for (const Command& cmd : pending)
    {
        ReplayResult result = ReplayCommand(cmd, db, manifest);

        // Flush definitively resolved commands (applied or duplicate).
        // Conflicted/rejected commands remain in the log for caller inspection.
        if (result == ReplayResult::Committed ||
            result == ReplayResult::Duplicate)
        {
            log.MarkFlushed(cmd.idempotencyKey);
        }
    }

    return manifest;
}
