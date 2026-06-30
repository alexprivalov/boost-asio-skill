# Market Data Feed — full-duplex framed-protocol example

A worked, compiled-and-tested example built **entirely from the `boost-asio-pro` skill**. It exercises the hardest parts of the skill: full-duplex I/O on one socket, a per-connection write queue, length-prefixed binary framing, a re-armable idle timeout, and graceful shutdown.

## Protocol

TCP, port 9090. Frame: `[4-byte big-endian length N][1-byte type][N-1 byte payload]`.

| Dir | Type | Meaning | Payload |
|-----|------|---------|---------|
| C→S | `0x01` SUBSCRIBE | start streaming a symbol | symbol, e.g. `AAPL` |
| C→S | `0x02` UNSUBSCRIBE | stop streaming | symbol |
| C→S | `0x03` PING | keepalive | — |
| S→C | `0x10` TICK | price update (pushed every 250ms) | `AAPL:142.55` |
| S→C | `0x11` PONG | ping reply | — |
| S→C | `0x12` ERROR | error text | text |

After SUBSCRIBE the server **pushes** TICKs while still reading further client frames on the same socket — full duplex. Reads and writes are serialized via a per-connection strand plus a write queue (a strand alone does not stop `async_write`s from interleaving bytes).

## Build & run (macOS, Homebrew Boost)

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build
./build/feed_server          # listens on :9090
python3 test_client.py       # integration test, in another terminal
```

## Build & run (Linux, Docker)

```bash
# Ubuntu 24.04 — builds with g++ and the skill's -fcoroutines guard
docker build -f Dockerfile.linux --build-arg BASE=ubuntu:24.04 -t feed .
docker run --rm -p 9090:9090 feed
python3 test_client.py
```

> Requires **Boost ≥ 1.77** for `awaitable_operators.hpp`. Debian *bookworm* (Boost 1.74) will not compile this — use Debian *trixie*, Ubuntu 24.04, or vendored Boost.

## Verified

| Platform | Compiler | Boost | Build | `test_client.py` |
|----------|----------|-------|-------|------------------|
| macOS (brew) | Apple clang 17 | 1.90 | ✅ | ✅ all pass |
| Ubuntu 24.04 (docker) | g++ 13.3 | apt | ✅ | ✅ all pass |
| Debian trixie (docker) | g++ 14.2 | 1.83 | ✅ | — |
| Debian bookworm (docker) | g++ 12 | 1.74 | ❌ (Boost too old) | — |
