#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "CommandLog.hpp"
#include "Database.hpp"
#include "Replay.hpp"
#include "SQLiteCommandLog.hpp"

// ── Timing helper ─────────────────────────────────────────────────────────────
using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::duration<double, std::milli>;

static double elapsed_ms(Clock::time_point start)
{
    return std::chrono::duration_cast<Ms>(Clock::now() - start).count();
}

// ── Command generation ────────────────────────────────────────────────────────

// Generate `count` commands targeting `entityCount` distinct entities.
// `conflictFraction` controls what fraction of non-commutative commands
// use a stale expectedVersion (guaranteed conflict on replay).
static std::vector<Command> GenerateCommands(int count,
                                             int entityCount,
                                             double conflictFraction,
                                             std::mt19937& rng)
{
    static const std::vector<std::string> nonCommutativeTypes = {
        "CreateBuilding", "DestroyBuilding", "TransferItem", "CompleteQuest"
    };
    static const std::vector<std::string> commutativeTypes = {
        "ResourceDelta", "ReputationDelta"
    };

    std::uniform_int_distribution<int> entityDist(0, entityCount - 1);
    std::uniform_real_distribution<double> chanceDist(0.0, 1.0);
    std::uniform_int_distribution<int> typeDist(0, (int)nonCommutativeTypes.size() - 1);
    std::uniform_int_distribution<int> deltaDist(-50, 50);

    std::vector<Command> cmds;
    cmds.reserve(count);

    for (int i = 0; i < count; ++i)
    {
        Command c;
        c.timestamp       = i;
        c.idempotencyKey  = "key-" + std::to_string(i);
        c.entityId        = "entity-" + std::to_string(entityDist(rng));

        bool isCommutative = chanceDist(rng) < 0.3;  // 30% commutative

        if (isCommutative)
        {
            c.type  = commutativeTypes[i % commutativeTypes.size()];
            c.delta = deltaDist(rng);
            c.expectedVersion = 0;  // ignored for commutative
        }
        else
        {
            c.type = nonCommutativeTypes[typeDist(rng)];
            // Stale version → guaranteed Conflict on replay
            bool isConflict = chanceDist(rng) < conflictFraction;
            c.expectedVersion = isConflict ? -1 : 0;  // entities start at v0
        }

        cmds.push_back(c);
    }
    return cmds;
}

// Seed the in-memory database with all entities at version 0.
static void SeedDatabase(Database& db, int entityCount)
{
    for (int i = 0; i < entityCount; ++i)
        db.AddEntity("entity-" + std::to_string(i), 0);
}

// ── Experiment 1: replay throughput vs log size (in-memory) ──────────────────
static void BenchReplayThroughput()
{
    std::cout << "\n=== Experiment 1: Replay throughput vs command log size ===\n";
    std::cout << std::left
              << std::setw(12) << "Commands"
              << std::setw(14) << "Total (ms)"
              << std::setw(20) << "Throughput (cmd/s)"
              << std::setw(12) << "Applied"
              << "Failed\n";
    std::cout << std::string(70, '-') << "\n";

    std::mt19937 rng(42);

    for (int count : {100, 500, 1000, 5000, 10000, 50000})
    {
        // Use one unique entity per command to eliminate intra-session conflicts
        // and measure pure throughput of the replay loop.
        const int entityCount = count;
        auto cmds = GenerateCommands(count, entityCount, 0.0, rng);

        Database db;
        SeedDatabase(db, entityCount);

        CommandLog log;
        for (const auto& c : cmds) log.Append(c);

        auto t0 = Clock::now();
        RejectionManifest manifest = ReplayAll(log, db);
        double ms = elapsed_ms(t0);

        // After ReplayAll: applied = committed + duplicate, failed = manifest entries
        int failed  = (int)manifest.size();
        int applied = count - failed;
        double throughput = (ms > 0) ? (count / ms * 1000.0) : 0.0;

        std::cout << std::left
                  << std::setw(12) << count
                  << std::setw(14) << std::fixed << std::setprecision(2) << ms
                  << std::setw(20) << std::fixed << std::setprecision(0) << throughput
                  << std::setw(12) << applied
                  << failed << "\n";
    }
}

// ── Experiment 2: in-memory CommandLog vs SQLiteCommandLog ───────────────────
static void BenchCommandLogBackends()
{
    std::cout << "\n=== Experiment 2: In-memory vs SQLite command log ===\n";
    std::cout << std::left
              << std::setw(12) << "Commands"
              << std::setw(18) << "Memory Append (ms)"
              << std::setw(20) << "SQLite Append (ms)"
              << std::setw(20) << "Memory GetPend (ms)"
              << "SQLite GetPend (ms)\n";
    std::cout << std::string(90, '-') << "\n";

    std::mt19937 rng(7);
    const int entityCount = 100;

    for (int count : {100, 500, 1000, 5000, 10000})
    {
        auto cmds = GenerateCommands(count, entityCount, 0.0, rng);

        // In-memory append
        CommandLog memLog;
        auto t0 = Clock::now();
        for (const auto& c : cmds) memLog.Append(c);
        double memAppend = elapsed_ms(t0);

        // SQLite append (":memory:" — no disk I/O, still goes through SQLite engine)
        SQLiteCommandLog sqlLog(":memory:");
        t0 = Clock::now();
        for (const auto& c : cmds) sqlLog.Append(c);
        double sqlAppend = elapsed_ms(t0);

        // GetPending: in-memory
        t0 = Clock::now();
        auto pending1 = memLog.GetPending();
        double memGet = elapsed_ms(t0);

        // GetPending: SQLite
        t0 = Clock::now();
        auto pending2 = sqlLog.GetPending();
        double sqlGet = elapsed_ms(t0);

        (void)pending1; (void)pending2;

        std::cout << std::left
                  << std::setw(12) << count
                  << std::setw(18) << std::fixed << std::setprecision(2) << memAppend
                  << std::setw(20) << std::fixed << std::setprecision(2) << sqlAppend
                  << std::setw(20) << std::fixed << std::setprecision(2) << memGet
                  << std::fixed << std::setprecision(2) << sqlGet << "\n";
    }
}

// ── Experiment 3: conflict rate impact on replay overhead ────────────────────
static void BenchConflictRate()
{
    std::cout << "\n=== Experiment 3: Effect of conflict rate on replay time ===\n";
    std::cout << std::left
              << std::setw(18) << "Conflict rate (%)"
              << std::setw(14) << "Total (ms)"
              << std::setw(18) << "Throughput (cmd/s)"
              << std::setw(12) << "Committed"
              << "Rejected\n";
    std::cout << std::string(75, '-') << "\n";

    const int count       = 10000;
    const int entityCount = 500;

    for (double rate : {0.0, 0.1, 0.25, 0.5, 0.75, 1.0})
    {
        std::mt19937 rng(99);
        auto cmds = GenerateCommands(count, entityCount, rate, rng);

        Database db;
        SeedDatabase(db, entityCount);

        CommandLog log;
        for (const auto& c : cmds) log.Append(c);

        auto t0 = Clock::now();
        RejectionManifest manifest = ReplayAll(log, db);
        double ms = elapsed_ms(t0);

        int failed    = (int)manifest.size();
        int applied   = count - failed;
        double throughput = (ms > 0) ? (count / ms * 1000.0) : 0.0;

        std::cout << std::left
                  << std::setw(18) << std::fixed << std::setprecision(0) << (rate * 100)
                  << std::setw(14) << std::fixed << std::setprecision(2) << ms
                  << std::setw(18) << std::fixed << std::setprecision(0) << throughput
                  << std::setw(12) << applied
                  << failed << "\n";
    }
}

// ── Experiment 4: SQLite full replay (append + GetPending + ReplayAll) ────────
static void BenchSQLiteFullReplay()
{
    std::cout << "\n=== Experiment 4: Full offline session replay via SQLite ===\n";
    std::cout << std::left
              << std::setw(12) << "Commands"
              << std::setw(18) << "Append (ms)"
              << std::setw(18) << "GetPending (ms)"
              << std::setw(18) << "ReplayAll (ms)"
              << "Total (ms)\n";
    std::cout << std::string(80, '-') << "\n";

    std::mt19937 rng(13);
    const int entityCount = 200;

    for (int count : {100, 500, 1000, 5000, 10000})
    {
        auto cmds = GenerateCommands(count, entityCount, 0.1, rng);

        Database db;
        SeedDatabase(db, entityCount);

        SQLiteCommandLog sqlLog(":memory:");

        auto t0 = Clock::now();
        for (const auto& c : cmds) sqlLog.Append(c);
        double appendMs = elapsed_ms(t0);

        t0 = Clock::now();
        auto pending = sqlLog.GetPending();
        double getMs = elapsed_ms(t0);

        // Replay against in-memory db (server-side is always in-memory/SQL in production)
        t0 = Clock::now();
        RejectionManifest manifest;
        for (const auto& c : pending)
        {
            ReplayResult r = ReplayCommand(c, db, manifest);
            if (r == ReplayResult::Committed || r == ReplayResult::Duplicate)
                sqlLog.MarkFlushed(c.idempotencyKey);
        }
        double replayMs = elapsed_ms(t0);

        double total = appendMs + getMs + replayMs;

        std::cout << std::left
                  << std::setw(12) << count
                  << std::setw(18) << std::fixed << std::setprecision(2) << appendMs
                  << std::setw(18) << std::fixed << std::setprecision(2) << getMs
                  << std::setw(18) << std::fixed << std::setprecision(2) << replayMs
                  << std::fixed << std::setprecision(2) << total << "\n";
    }
}

int main()
{
    std::cout << "OfflineSync Benchmark Suite\n";
    std::cout << std::string(80, '=') << "\n";

    BenchReplayThroughput();
    BenchCommandLogBackends();
    BenchConflictRate();
    BenchSQLiteFullReplay();

    std::cout << "\nDone.\n";
    return 0;
}
