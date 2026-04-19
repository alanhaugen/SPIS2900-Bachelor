#include <catch2/catch_test_macros.hpp>
#include "CommandLog.hpp"
#include "Database.hpp"
#include "Replay.hpp"

static Command MakeCommand(const std::string& type,
                           const std::string& idempotencyKey,
                           const std::string& entityId,
                           int32_t            expectedVersion = 0,
                           int32_t            delta           = 0,
                           int64_t            timestamp       = 1000)
{
    Command c;
    c.type            = type;
    c.idempotencyKey  = idempotencyKey;
    c.entityId        = entityId;
    c.expectedVersion = expectedVersion;
    c.delta           = delta;
    c.timestamp       = timestamp;
    return c;
}

// ─────────────────────────────────────────────────────────────────────────────
// CommandLog
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("CommandLog: append increases pending count", "[commandlog]")
{
    CommandLog log;
    REQUIRE(log.PendingCount() == 0);

    log.Append(MakeCommand("CompleteQuest", "key-1", "quest-42"));
    REQUIRE(log.PendingCount() == 1);

    log.Append(MakeCommand("CompleteQuest", "key-2", "quest-43"));
    REQUIRE(log.PendingCount() == 2);
}

TEST_CASE("CommandLog: GetPending returns entries in insertion order", "[commandlog]")
{
    CommandLog log;
    log.Append(MakeCommand("CompleteQuest", "key-A", "quest-1", 0, 0, 100));
    log.Append(MakeCommand("CompleteQuest", "key-B", "quest-2", 0, 0, 200));

    auto pending = log.GetPending();
    REQUIRE(pending.size() == 2);
    REQUIRE(pending[0].idempotencyKey == "key-A");
    REQUIRE(pending[1].idempotencyKey == "key-B");
    REQUIRE(pending[0].timestamp < pending[1].timestamp);
}

TEST_CASE("CommandLog: MarkFlushed removes the matching entry", "[commandlog]")
{
    CommandLog log;
    log.Append(MakeCommand("CompleteQuest", "key-1", "quest-1"));
    log.Append(MakeCommand("CompleteQuest", "key-2", "quest-2"));

    log.MarkFlushed("key-1");
    REQUIRE(log.PendingCount() == 1);
    REQUIRE(log.GetPending()[0].idempotencyKey == "key-2");

    log.MarkFlushed("key-2");
    REQUIRE(log.PendingCount() == 0);
}

TEST_CASE("CommandLog: MarkFlushed with unknown key is a no-op", "[commandlog]")
{
    CommandLog log;
    log.Append(MakeCommand("CompleteQuest", "key-1", "quest-1"));
    log.MarkFlushed("nonexistent");
    REQUIRE(log.PendingCount() == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Idempotency
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Idempotency: replaying the same command twice yields Duplicate on second attempt",
          "[idempotency]")
{
    Database          db;
    RejectionManifest manifest;
    db.AddEntity("building-1", 0);

    Command cmd = MakeCommand("DestroyBuilding", "idem-key-1", "building-1", 0);

    REQUIRE(ReplayCommand(cmd, db, manifest) == ReplayResult::Committed);
    REQUIRE(manifest.empty());

    REQUIRE(ReplayCommand(cmd, db, manifest) == ReplayResult::Duplicate);
    REQUIRE(manifest.empty());   // Duplicate is silent
}

// ─────────────────────────────────────────────────────────────────────────────
// Commutative merge
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Commutative: ResourceDelta applied even on version mismatch", "[commutative]")
{
    Database          db;
    RejectionManifest manifest;
    db.AddEntity("resource-node", 5, 100);   // server is at version 5

    Command cmd = MakeCommand("ResourceDelta", "res-key-1", "resource-node",
                              /*expectedVersion=*/0, /*delta=*/50);

    REQUIRE(ReplayCommand(cmd, db, manifest) == ReplayResult::Committed);
    REQUIRE(manifest.empty());

    Entity e = db.GetEntity("resource-node");
    REQUIRE(e.value   == 150);
    REQUIRE(e.version == 6);
}

TEST_CASE("Commutative: ReputationDelta applied even on version mismatch", "[commutative]")
{
    Database          db;
    RejectionManifest manifest;
    db.AddEntity("faction-nord", 3, -10);

    Command cmd = MakeCommand("ReputationDelta", "rep-key-1", "faction-nord",
                              /*expectedVersion=*/0, /*delta=*/+25);

    REQUIRE(ReplayCommand(cmd, db, manifest) == ReplayResult::Committed);
    REQUIRE(db.GetEntity("faction-nord").value == 15);
}

TEST_CASE("Commutative: two ResourceDeltas from concurrent clients both apply", "[commutative]")
{
    Database          db;
    RejectionManifest manifest;
    db.AddEntity("gold-chest", 0, 0);

    ReplayCommand(MakeCommand("ResourceDelta", "delta-1", "gold-chest", 0, +10), db, manifest);
    ReplayCommand(MakeCommand("ResourceDelta", "delta-2", "gold-chest", 0, +20), db, manifest);

    Entity e = db.GetEntity("gold-chest");
    REQUIRE(e.value   == 30);
    REQUIRE(e.version == 2);
    REQUIRE(manifest.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Non-commutative: success
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Non-commutative: version matches → Committed and version increments",
          "[non_commutative]")
{
    Database          db;
    RejectionManifest manifest;
    db.AddEntity("building-7", 3);

    Command cmd = MakeCommand("DestroyBuilding", "destroy-1", "building-7", 3);

    REQUIRE(ReplayCommand(cmd, db, manifest) == ReplayResult::Committed);
    REQUIRE(manifest.empty());
    REQUIRE(db.GetEntity("building-7").version == 4);
}

// ─────────────────────────────────────────────────────────────────────────────
// Non-commutative: conflict
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Non-commutative: version mismatch → Conflict in manifest, entity unchanged",
          "[non_commutative]")
{
    Database          db;
    RejectionManifest manifest;
    db.AddEntity("building-9", 5);   // server advanced to version 5

    Command cmd = MakeCommand("DestroyBuilding", "destroy-2", "building-9",
                              /*expectedVersion=*/2);   // stale

    REQUIRE(ReplayCommand(cmd, db, manifest) == ReplayResult::Conflict);
    REQUIRE(manifest.size() == 1);
    REQUIRE(manifest[0].reason == ReplayResult::Conflict);
    REQUIRE(manifest[0].command.idempotencyKey == "destroy-2");
    REQUIRE(db.GetEntity("building-9").version == 5);   // unchanged
}

// ─────────────────────────────────────────────────────────────────────────────
// Non-commutative: rejected
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Non-commutative: entity deleted server-side → Rejected in manifest",
          "[non_commutative]")
{
    Database          db;
    RejectionManifest manifest;
    db.AddEntity("building-3", 1);
    db.DeleteEntity("building-3");

    Command cmd = MakeCommand("DestroyBuilding", "destroy-3", "building-3", 1);

    REQUIRE(ReplayCommand(cmd, db, manifest) == ReplayResult::Rejected);
    REQUIRE(manifest.size() == 1);
    REQUIRE(manifest[0].reason == ReplayResult::Rejected);
}

TEST_CASE("Non-commutative: entity never existed → Rejected in manifest",
          "[non_commutative]")
{
    Database          db;
    RejectionManifest manifest;

    Command cmd = MakeCommand("DestroyBuilding", "destroy-4", "ghost-building", 0);

    REQUIRE(ReplayCommand(cmd, db, manifest) == ReplayResult::Rejected);
    REQUIRE(manifest.size() == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Full offline session integration test
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Full offline session: partial success, manifest contains only failures",
          "[integration]")
{
    // Server state at reconnect time:
    //   building-A  version 2  (was 1 when client went offline → stale → Conflict)
    //   building-B  version 0  (untouched → clean commit)
    //   building-C  deleted    (→ Rejected)
    //   faction-X   version 0, value 0   (reputation delta → always applies)
    //   resource-Y  version 0, value 50  (resource delta   → always applies)
    Database db;
    db.AddEntity("building-A", 2);
    db.AddEntity("building-B", 0);
    db.AddEntity("building-C", 1);
    db.DeleteEntity("building-C");
    db.AddEntity("faction-X",  0, 0);
    db.AddEntity("resource-Y", 0, 50);

    CommandLog log;
    log.Append(MakeCommand("DestroyBuilding", "cmd-1", "building-A", 1));     // Conflict
    log.Append(MakeCommand("DestroyBuilding", "cmd-2", "building-B", 0));     // Committed
    log.Append(MakeCommand("DestroyBuilding", "cmd-3", "building-C", 1));     // Rejected
    log.Append(MakeCommand("ReputationDelta", "cmd-4", "faction-X",  99, -5));// Committed (commutative)
    log.Append(MakeCommand("ResourceDelta",   "cmd-5", "resource-Y", 0, +30));// Committed (commutative)
    log.Append(MakeCommand("ResourceDelta",   "cmd-5", "resource-Y", 0, +30));// Duplicate (retransmit)

    REQUIRE(log.PendingCount() == 6);

    RejectionManifest manifest = ReplayAll(log, db);

    // Two failures in the manifest
    REQUIRE(manifest.size() == 2);
    bool hasConflict = false;
    bool hasRejected = false;
    for (const auto& e : manifest)
    {
        if (e.command.idempotencyKey == "cmd-1" && e.reason == ReplayResult::Conflict)
            hasConflict = true;
        if (e.command.idempotencyKey == "cmd-3" && e.reason == ReplayResult::Rejected)
            hasRejected = true;
    }
    REQUIRE(hasConflict);
    REQUIRE(hasRejected);

    // Only the two failed commands remain pending in the log
    REQUIRE(log.PendingCount() == 2);

    // Entity state
    REQUIRE(db.GetEntity("building-A").version == 2);  // unchanged (Conflict)
    REQUIRE(db.GetEntity("building-B").version == 1);  // committed
    REQUIRE(db.GetEntity("faction-X").value    == -5); // delta applied
    REQUIRE(db.GetEntity("resource-Y").value   == 80); // delta applied once (duplicate skipped)
    REQUIRE(db.GetEntity("resource-Y").version == 1);  // incremented once only
}
