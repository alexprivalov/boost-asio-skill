cmake_minimum_required(VERSION 3.20)
project(framed_protocol CXX)

# Boost.Asio is header-only since Boost 1.74+.
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

add_executable(feed_server feed_server.cpp)

target_link_libraries(feed_server PRIVATE
    Boost::headers          # header-only Asio
    Threads::Threads
)

target_compile_features(feed_server PRIVATE cxx_std_20)

# REQUIRED for GCC coroutine support — build will fail without this.
target_compile_options(feed_server PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-fcoroutines>
)

# Truly header-only (no Boost.System link needed).
target_compile_definitions(feed_server PRIVATE BOOST_ERROR_CODE_HEADER_ONLY)
