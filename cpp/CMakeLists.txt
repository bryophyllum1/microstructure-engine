cmake_minimum_required(VERSION 3.25)
project(msengine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # C4324: "structure was padded due to alignment specifier" — the padding
    # in SpscQueue is intentional (false-sharing protection).
    add_compile_options(/W4 /permissive- /wd4324)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ixwebsocket CONFIG REQUIRED)
find_package(simdjson CONFIG REQUIRED)

# --- Core engine library ---
add_library(msengine_core
    src/order_book.cpp
    src/binance_feed.cpp
)
target_include_directories(msengine_core PUBLIC include)
target_link_libraries(msengine_core PUBLIC ixwebsocket::ixwebsocket simdjson::simdjson)
if(WIN32)
    # mbedTLS entropy source uses BCryptGenRandom
    target_link_libraries(msengine_core PUBLIC bcrypt)
endif()

# --- Apps ---
add_executable(feed_dump apps/feed_dump.cpp)
target_link_libraries(feed_dump PRIVATE msengine_core)

# --- Tests ---
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
# Use shared CRT on Windows to match default MSVC settings.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(msengine_tests
    tests/order_book_test.cpp
    tests/binance_parser_test.cpp
    tests/spsc_queue_test.cpp
    tests/signal_engine_test.cpp
)
target_link_libraries(msengine_tests PRIVATE msengine_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(msengine_tests)
