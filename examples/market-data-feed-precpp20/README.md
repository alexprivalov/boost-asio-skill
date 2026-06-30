# Market Data Feed — pre-C++20 (callback) variant

The same full-duplex framed protocol as [`../market-data-feed/`](../market-data-feed/), but written in the **pre-C++20 callback style** from the skill's "Before Coroutines" section — no `co_await`, no `awaitable`, no `awaitable_operators`. Built entirely from the skill and compiled with `-std=c++17`.

Why it exists: it shows the skill's pre-coroutine patterns (callback `shared_from_this` sessions, `bind_executor` strands, the write queue, a watchdog timer, and a self-rescheduling per-symbol ticker) **and** it compiles on much older Boost than the coroutine version.

## Protocol

Identical to the coroutine example (port **9092**). Frame: `[4B big-endian len N][1B type][N-1B payload]`. See [../market-data-feed/README.md](../market-data-feed/README.md) for the message table.

## Build & run

```bash
# macOS (Homebrew Boost)
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew && cmake --build build
./build/feed_server
FEED_PORT=9092 python3 test_client.py

# Linux (Docker) — works on OLD Boost too
docker build -f Dockerfile.linux --build-arg BASE=debian:bookworm -t feed-pc .
docker run --rm -p 9092:9092 feed-pc
FEED_PORT=9092 python3 test_client.py
```

## Oldest Boost, and C++ standards

- **Oldest compatible Boost: 1.74.** The callback strand uses `asio::any_io_executor`, introduced in Boost 1.74 (Asio 1.18). Verified: builds on 1.74 (Debian bookworm), fails on 1.71 (Ubuntu 20.04, `'any_io_executor' is not a member of 'asio'`). `CMakeLists.txt` enforces this with `find_package(Boost 1.74 REQUIRED)`. To go older, swap in the legacy `asio::io_context::strand`.
- **C++ standard: C++11 and up.** The code is C++11-clean (no `co_await`, no `std::chrono_literals` — uses `std::chrono::milliseconds(250)` etc.). Verified building and running at `-std=c++11`, `c++14`, and `c++17`. Override with `-DCMAKE_CXX_STANDARD=11`.

## Verified (all in CI)

| Platform | Compiler | Boost | Build | `test_client.py` |
|----------|----------|-------|-------|------------------|
| macOS | Apple clang 17 | 1.90 | ✅ | ✅ |
| Ubuntu 24.04 | g++ 13.3 | 1.83 | ✅ | ✅ |
| Debian trixie | g++ 14.2 | 1.83 | ✅ | — |
| **Debian bookworm** | **g++ 12.2** | **1.74** | **✅** | **✅** (c++11/14/17) |
| Fedora 44 | g++ 16.1 | 1.90 | ✅ | ✅ |
| **Windows** | **MSVC (VS 2022)** | runner Boost | ✅ | ✅ |

> Contrast: the C++20 coroutine example fails on bookworm (Boost 1.74 lacks `awaitable_operators.hpp`), and the **stackful `asio::spawn`** style would also fail there (3-arg signature needs Boost ≥ 1.80). Only this callback style spans the whole range — and, thanks to the portable big-endian codec, Windows/MSVC too.
