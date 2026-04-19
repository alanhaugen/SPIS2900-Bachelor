#pragma once
#include "Command.hpp"
#include "CommandLog.hpp"
#include "Database.hpp"
#include <vector>

// A single rejected command together with the reason it could not be applied.
struct RejectionEntry
{
    Command      command;
    ReplayResult reason;   // always Conflict or Rejected
};

using RejectionManifest = std::vector<RejectionEntry>;

// ReplayCommand
// -------------
// Implements the conflict-resolution algorithm from Section 3.3 of the thesis.
//
// Decision tree (in order):
//   1. db.IsAlreadyApplied(cmd.idempotencyKey) → Duplicate (silent, no manifest entry)
//   2. cmd.type == "ResourceDelta" || "ReputationDelta"
//        → commutative path: db.ApplyDelta(cmd), return Committed
//   3. !entity.exists  → Rejected  (entry added to manifest)
//   4. entity.version != cmd.expectedVersion → Conflict (entry added to manifest)
//   5. otherwise       → db.Commit(cmd), return Committed
ReplayResult ReplayCommand(const Command& cmd, Database& db,
                           RejectionManifest& manifest);

// ReplayAll
// ---------
// Iterates pending commands in order and calls ReplayCommand for each.
// Calls log.MarkFlushed for every Committed or Duplicate command.
// Conflicted/Rejected commands remain in the log; their entries appear in the
// returned manifest so the caller can surface them to the player.
RejectionManifest ReplayAll(CommandLog& log, Database& db);
