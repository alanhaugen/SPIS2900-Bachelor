#pragma once
#include <cstdint>

// Abstract base class that mirrors the DBI described in the thesis.
// Online implementations invoke SQL stored procedures; offline implementations
// route calls to SQLite or a local command log.
class IDatabaseInterface
{
public:
    virtual ~IDatabaseInterface() = default;

    virtual bool CreateBuilding(int32_t playerId, int32_t buildingTypeId,
                                float posX, float posY, float posZ) = 0;
    virtual bool DestroyBuilding(int32_t buildingId) = 0;

    virtual bool TransferItem(int32_t fromPlayerId, int32_t toPlayerId,
                              int32_t itemId, int32_t quantity) = 0;

    virtual bool CompleteQuest(int32_t playerId, int32_t questId) = 0;

    virtual bool UpdateFactionRelation(int32_t factionA, int32_t factionB,
                                       int32_t relationDelta) = 0;
};
