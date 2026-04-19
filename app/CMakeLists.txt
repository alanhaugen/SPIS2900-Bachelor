cmake_minimum_required(VERSION 3.20)
project(OfflineSync CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Catch2 ────────────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

# ── SQLite3 (use local system library, prefer Homebrew over Mono) ─────────────
find_library(SQLITE3_LIB sqlite3
    HINTS /opt/homebrew/opt/sqlite/lib /usr/lib
    NO_DEFAULT_PATH
)
if(NOT SQLITE3_LIB)
    find_library(SQLITE3_LIB sqlite3)   # fall back to any system path
endif()

find_path(SQLITE3_INCLUDE sqlite3.h
    HINTS /opt/homebrew/opt/sqlite/include /usr/include
    NO_DEFAULT_PATH
)
if(NOT SQLITE3_INCLUDE)
    find_path(SQLITE3_INCLUDE sqlite3.h)
endif()

message(STATUS "SQLite3 library : ${SQLITE3_LIB}")
message(STATUS "SQLite3 headers : ${SQLITE3_INCLUDE}")

add_library(sqlite3_iface INTERFACE)
target_link_libraries(sqlite3_iface INTERFACE ${SQLITE3_LIB})
target_include_directories(sqlite3_iface INTERFACE ${SQLITE3_INCLUDE})

# ── Core persistence library (no SQLite dependency) ───────────────────────────
add_library(offline_sync_lib
    src/CommandLog.cpp
    src/Database.cpp
    src/Replay.cpp
)
target_include_directories(offline_sync_lib PUBLIC include)

# ── SQLite implementations ────────────────────────────────────────────────────
add_library(offline_sync_sqlite_lib
    src/SQLiteDBI.cpp
    src/SQLiteCommandLog.cpp
)
target_include_directories(offline_sync_sqlite_lib PUBLIC include)
target_link_libraries(offline_sync_sqlite_lib PUBLIC sqlite3_iface)

# ── Tests (core) ──────────────────────────────────────────────────────────────
add_executable(offline_sync_tests tests/tests.cpp)
target_link_libraries(offline_sync_tests
    PRIVATE
        offline_sync_lib
        Catch2::Catch2WithMain
)

# ── Tests (SQLite) ────────────────────────────────────────────────────────────
add_executable(offline_sync_sqlite_tests tests/test_sqlite.cpp)
target_link_libraries(offline_sync_sqlite_tests
    PRIVATE
        offline_sync_sqlite_lib
        Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(offline_sync_tests)
catch_discover_tests(offline_sync_sqlite_tests)

# ── Benchmarks ────────────────────────────────────────────────────────────────
add_executable(offline_sync_benchmarks benchmarks/benchmark.cpp)
target_link_libraries(offline_sync_benchmarks
    PRIVATE
        offline_sync_lib
        offline_sync_sqlite_lib
)
