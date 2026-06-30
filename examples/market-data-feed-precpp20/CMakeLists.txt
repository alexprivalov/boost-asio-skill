cmake_minimum_required(VERSION 3.16)
project(feed_server CXX)

# Pre-C++20 callback style: C++17, no coroutines.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # emit -std=c++17 (not gnu++17)

find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

add_executable(feed_server feed_server.cpp)

# Header-only Asio: Boost::headers + BOOST_ERROR_CODE_HEADER_ONLY (skill build note).
# Callback style needs no -fcoroutines (that is only for C++20 co_await).
target_link_libraries(feed_server PRIVATE Boost::headers Threads::Threads)
target_compile_definitions(feed_server PRIVATE BOOST_ERROR_CODE_HEADER_ONLY)
