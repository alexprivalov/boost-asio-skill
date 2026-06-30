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

## Verified

The callback style has **no minimum-Boost floor** for coroutine headers, so it builds where the coroutine example cannot:

| Platform | Compiler | Boost | Build | `test_client.py` |
|----------|----------|-------|-------|------------------|
| macOS (brew) | Apple clang 17 | 1.90 | ✅ | ✅ all pass |
| Ubuntu 24.04 | g++ 13.3 | 1.83 | ✅ | ✅ all pass |
| Debian trixie | g++ 14.2 | 1.83 | ✅ | — |
| **Debian bookworm** | **g++ 12.2** | **1.74** | **✅** | **✅ all pass** |

> Contrast: the C++20 coroutine example fails on bookworm (Boost 1.74 lacks `awaitable_operators.hpp`). Note also that the **stackful `asio::spawn`** style would *not* build on bookworm either — its 3-arg signature needs Boost ≥ 1.80. Only the callback style spans the whole range.
