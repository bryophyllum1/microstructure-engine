cmake_minimum_required(VERSION 3.25)
project(msengine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- Core engine library ---
add_library(msengine_core
    src/order_book.cpp
)
target_include_directories(msengine_core PUBLIC include)

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
add_executable(order_book_test tests/order_book_test.cpp)
target_link_libraries(order_book_test PRIVATE msengine_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(order_book_test)
