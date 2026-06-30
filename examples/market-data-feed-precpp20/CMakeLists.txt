cmake_minimum_required(VERSION 3.16)
project(feed_server CXX)

# Pre-C++20 callback style: no coroutines. Code is C++11-clean; default to 17
# but allow -DCMAKE_CXX_STANDARD=11/14/17 to verify each standard.
if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # emit -std=c++NN (not gnu++NN)

# Floor is Boost 1.74: the callback strand uses asio::any_io_executor (added in 1.74).
find_package(Boost 1.74 REQUIRED)
find_package(Threads REQUIRED)

add_executable(feed_server feed_server.cpp)

# Header-only Asio: Boost::headers + BOOST_ERROR_CODE_HEADER_ONLY (skill build note).
# Callback style needs no -fcoroutines (that is only for C++20 co_await).
target_link_libraries(feed_server PRIVATE Boost::headers Threads::Threads)
target_compile_definitions(feed_server PRIVATE BOOST_ERROR_CODE_HEADER_ONLY)
