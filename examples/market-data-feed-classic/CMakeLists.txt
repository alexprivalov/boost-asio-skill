cmake_minimum_required(VERSION 3.5)   # low enough for Debian 9's cmake 3.7
project(feed_server CXX)

# Classic style is C++11-clean; default to 11 but allow override to verify 11/14/17.
if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 11)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED)        # headers (every Boost version)
find_package(Threads REQUIRED)

add_executable(feed_server feed_server.cpp)
target_include_directories(feed_server PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(feed_server PRIVATE Threads::Threads)

# Old Boost (< 1.69) needs the compiled Boost.System lib for error_code's symbols;
# modern Boost is header-only and has no such component. Link it only if present.
find_package(Boost QUIET COMPONENTS system)
if(Boost_SYSTEM_FOUND)
    target_link_libraries(feed_server PRIVATE Boost::system)
endif()
