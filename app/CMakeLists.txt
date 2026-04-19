cmake_minimum_required(VERSION 3.20)
project(OfflineSync CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

add_library(offline_sync_lib
    src/CommandLog.cpp
    src/Database.cpp
    src/Replay.cpp
)
target_include_directories(offline_sync_lib PUBLIC include)

add_executable(offline_sync_tests tests/tests.cpp)
target_link_libraries(offline_sync_tests
    PRIVATE
        offline_sync_lib
        Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(offline_sync_tests)
