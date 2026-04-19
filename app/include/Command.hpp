#pragma once
#include <cstdint>
#include <string>

// A serializable record of one player action taken during an offline session.
//
// type             – discriminates how the command is replayed:
//                    "CreateBuilding", "DestroyBuilding", "TransferItem",
//                    "CompleteQuest", "ResourceDelta", "ReputationDelta"
// timestamp        – UTC milliseconds at the moment the action was taken
// idempotencyKey   – UUID string; used to detect duplicate replays
// entityId         – identifies the entity the command targets
// expectedVersion  – optimistic-concurrency snapshot of entity.version at the
//                    time the command was generated (non-commutative ops only)
// delta            – signed magnitude for commutative (additive) operations
struct Command
{
    std::string type;
    int64_t     timestamp       = 0;
    std::string idempotencyKey;
    std::string entityId;
    int32_t     expectedVersion = 0;
    int32_t     delta           = 0;
};
