# boost-asio-pro

An agent skill (for [Claude Code](https://code.claude.com/docs), Codex, and other tools that read `SKILL.md`) for writing correct, production-grade asynchronous C++ networking code with **Boost.Asio** and **standalone Asio**.

## What is this?

Asio's API changed shape three times — classic `io_service` → `io_context` → C++20 coroutines — and most Asio code on the internet is from the first era. Models trained on that code reach for `io_service`, `strand.wrap`, and `boost::bind`, or write coroutine code that doesn't compile against the Boost the user actually has.

This skill makes the agent pick the style from the toolchain first, then apply the rules that are genuinely easy to get wrong:

- **Style selection** by Boost version × C++ standard (C++20 coroutines / callbacks / stackful `spawn` / classic `io_service`)
- **Version floors verified by compiling**, not by reading docs (`awaitable_operators` ≥ 1.77, `as_tuple` ≥ 1.79, `any_io_executor` ≥ 1.74, `io_context` ≥ 1.66, …)
- **Strand + write queue**: a strand serializes handler execution, not composed operations — two `async_write`s still interleave bytes
- Buffer and connection lifetime rules, composed reads for framing, `operation_aborted` on re-armed timers
- SSL/TLS, CMake for Boost.Asio / standalone / dual-mode, and a portability shim between the two
- A pre-completion checklist the agent runs against its own code

## Install

### Claude Code (plugin — recommended)

```
/plugin marketplace add alexprivalov/boost-asio-skill
/plugin install boost-asio-pro@boost-asio-skill
```

### Claude Code / Codex (manual)

Skills are **directories** containing a `SKILL.md`, so copy the whole skill directory:

```bash
git clone https://github.com/alexprivalov/boost-asio-skill.git /tmp/boost-asio-skill

# Claude Code (personal)
cp -R /tmp/boost-asio-skill/skills/boost-asio-pro ~/.claude/skills/

# Codex and other tools that read ~/.agents
mkdir -p ~/.agents/skills && cp -R /tmp/boost-asio-skill/skills/boost-asio-pro ~/.agents/skills/

# Project-level
mkdir -p .claude/skills && cp -R /tmp/boost-asio-skill/skills/boost-asio-pro .claude/skills/
```

## Layout

```
skills/boost-asio-pro/
  SKILL.md                    # style selection, version floors, traps, checklist (always loaded)
  references/coroutines.md    # C++20 coroutine style
  references/pre-cpp20.md     # callbacks and stackful spawn (C++11–17)
  references/classic-boost.md # pre-1.66 io_service era
  references/ssl.md           # SSL/TLS (any style)
  references/build.md         # CMake, header-only usage
examples/                     # CI-verified servers, one per style
```

`SKILL.md` stays small so it costs little to load; the reference files are read only when the chosen style needs them.

## Coverage

| Topic | Patterns |
|-------|----------|
| Coroutines | `co_spawn`, `awaitable<T>`, `co_await`, `deferred`, `detached` |
| Error handling | `as_tuple`, `redirect_error`, exception-based |
| Thread safety | Strands, `bind_executor`, implicit vs explicit |
| Full-duplex | Per-connection strand **+ write queue**, lifetime via `shared_from_this` |
| SSL/TLS | Client/server, certificate verification, SNI |
| Timeouts | `awaitable_operators` (`\|\|`, `&&`), `steady_timer`, re-armable idle timeout, watchdog |
| Cancellation | `this_coro::cancellation_state`, `reset_cancellation_state` |
| Networking | TCP server, resolver/DNS, line-based + length-prefixed binary framing |
| Lifecycle | `signal_set` graceful shutdown, `async_accept(make_strand(...))` |
| Buffers | `buffer()`, `dynamic_buffer`, lifetime rules |
| Build | CMake for Boost.Asio, standalone Asio, dual-mode, minimum Boost versions |
| Pre-C++20 | Callbacks (`shared_from_this` + `bind_executor`) and stackful `asio::spawn`/`yield_context` |
| Classic | `io_service`, `strand.wrap`, `expires_from_now`, Boost.System linkage |
| Portability | Namespace shim for Boost.Asio ↔ standalone Asio |

## Worked examples

All three are written entirely from this skill and double as its regression tests — CI builds them and runs `test_client.py` on every push:

- [`examples/market-data-feed/`](examples/market-data-feed/) — full-duplex framed-protocol server, **C++20 coroutine** style. Verified on macOS (clang/Boost 1.90), Ubuntu 24.04, Debian trixie.
- [`examples/market-data-feed-precpp20/`](examples/market-data-feed-precpp20/) — same server, **pre-C++20 callback** style, compiled `-std=c++17` (C++11-clean). Verified on macOS, Ubuntu, Debian trixie, **Debian bookworm (Boost 1.74)**, Fedora, **Windows/MSVC**, at C++11/14/17.
- [`examples/market-data-feed-classic/`](examples/market-data-feed-classic/) — same server, **classic pre-`io_context` style** (`io_service`, `strand.wrap`, `expires_from_now`, linked Boost.System). Verified back to **Boost 1.62 (Debian 9, 2016)** and on current Boost.

**Boost floors by style:** coroutine ≥ 1.77 · pre-C++20 callback ≥ 1.74 · classic ≥ 1.62.

## Boost.Asio vs standalone Asio

Both are supported; the skill includes a portability shim:

```cpp
#ifdef USE_STANDALONE_ASIO
  #include <asio.hpp>
  namespace net = asio;
#else
  #include <boost/asio.hpp>
  namespace net = boost::asio;
#endif
```

## Official documentation

- Boost.Asio: https://www.boost.org/doc/libs/latest/doc/html/boost_asio.html
- Standalone Asio: https://think-async.com/Asio/

## License

MIT
