# Market Data Feed — classic / conservative Boost.Asio (very old Boost)

The same full-duplex framed protocol as the other two examples, written in the **classic pre-`io_context` style** so it compiles on **very old Boost** (verified back to **Boost 1.62**, Debian 9 / 2016) as well as current Boost 1.90. Port **9093**.

This is the example to copy if you're stuck on an old Boost (think Boost 1.49–1.65, the 2012–2017 era).

## What makes it conservative

It avoids everything added in the `io_context` era (Boost 1.66+):

| Modern (1.66+) | Classic (this example) |
|----------------|------------------------|
| `io_context` | `io_service` (aliased by Boost version) |
| `make_strand` / `any_io_executor` | `io_service::strand` |
| `bind_executor(strand, h)` | `strand.wrap(h)` |
| `expires_after(d)` | `expires_from_now(d)` (aliased) |
| move-return `async_accept()` | `async_accept(socket_, handler)` |
| header-only `error_code` | links **Boost.System** |
| `co_await` / `awaitable` | callback chains |

Only **two** things actually differ across Boost 1.62 … 1.90 — the `io_service`/`io_context` name and the timer-expiry call — and both are isolated in a tiny `#if BOOST_VERSION` shim at the top of `feed_server.cpp`. A portable big-endian codec (no `<arpa/inet.h>`) keeps it cross-platform too.

## Build & run

```bash
# Modern toolchains (cmake >= 3.13)
cmake -S . -B build && cmake --build build
./build/feed_server
FEED_PORT=9093 python3 ../market-data-feed-precpp20/test_client.py

# Old toolchains (cmake < 3.13, e.g. Ubuntu 18.04 / Debian 9): classic out-of-source build
mkdir build && cd build && cmake .. && make
```

## Verified

| Platform | Boost | Compiler | cmake | Build | Integration test |
|----------|-------|----------|-------|-------|------------------|
| **Debian 9** | **1.62** | g++ 6.3 | (direct g++) | ✅ | ✅ |
| **Ubuntu 18.04** | 1.65 | g++ 7.5 | 3.10 | ✅ | ✅ |
| macOS | 1.90 | Apple clang 17 | 4.x | ✅ | ✅ |
| Ubuntu (latest) | 1.83 | g++ | 3.2x | ✅ | ✅ |

> Contrast the floors: coroutine example needs Boost ≥ 1.77, the `precpp20` callback example ≥ 1.74 (`any_io_executor`), and this classic example ≥ 1.62 (and the API it uses dates to ~Boost 1.49).
