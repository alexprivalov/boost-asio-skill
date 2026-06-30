# boost-asio-pro

A [Claude Code](https://docs.anthropic.com/en/docs/agents-and-tools/claude-code/overview) / [Codex](https://openai.com/index/codex/) skill for writing correct, production-grade asynchronous C++ networking code with **Boost.Asio** and **standalone Asio**.

## What is this?

This is an AI agent skill file — a structured reference document that helps AI coding assistants (Claude Code, OpenAI Codex, GitHub Copilot) write better Boost.Asio / standalone Asio code by providing:

- Correct C++20 coroutine patterns (`co_await`, `awaitable`, `co_spawn`)
- Strand-based thread safety rules
- SSL/TLS stream handling
- Timeout patterns with `awaitable_operators`
- Composed async operations
- Build configuration (CMake) for both Boost.Asio and standalone Asio
- Common mistakes and their fixes

## Installation

### Claude Code

```bash
# Copy to your personal skills directory
cp SKILL.md ~/.claude/skills/boost-asio-pro.md
```

Or clone this repo:
```bash
git clone https://github.com/alexprivalov/boost-asio-skill.git ~/.claude/skills/boost-asio-pro
```

### Codex / Other Agents

```bash
mkdir -p ~/.agents/skills/boost-asio-pro
cp SKILL.md ~/.agents/skills/boost-asio-pro/SKILL.md
```

### As a project-level skill

Add to your project's `.claude/skills/` directory:
```bash
mkdir -p .claude/skills
cp SKILL.md .claude/skills/boost-asio-pro.md
```

## Coverage

| Topic | Patterns |
|-------|----------|
| Coroutines | `co_spawn`, `awaitable<T>`, `co_await`, `deferred`, `detached` |
| Error handling | `as_tuple`, `redirect_error`, exception-based |
| Thread safety | Strands, `bind_executor`, implicit vs explicit |
| Full-duplex | Per-connection strand **+ write queue** (a strand alone does not stop interleaved writes), connection lifetime with `shared_from_this` |
| SSL/TLS | Client/server, certificate verification, SNI |
| Timeouts | `awaitable_operators` (`\|\|`, `&&`), `steady_timer`, **re-armable idle timeout** |
| Cancellation | `this_coro::cancellation_state`, `reset_cancellation_state` |
| Networking | TCP server, resolver/DNS, line-based + **length-prefixed binary framing** |
| Lifecycle | `signal_set` graceful shutdown, `async_accept(make_strand(...))` |
| Buffers | `buffer()`, `dynamic_buffer`, lifetime rules |
| Build | CMake for Boost.Asio, standalone Asio, dual-mode, **minimum Boost versions** |
| Portability | Namespace shim for Boost.Asio ↔ standalone Asio |

## Worked example

[`examples/market-data-feed/`](examples/market-data-feed/) is a full-duplex framed-protocol server written entirely from this skill and **compiled + integration-tested** on macOS (clang, Homebrew Boost), Ubuntu 24.04, and Debian trixie. It doubles as the skill's regression test: if the skill is correct, the example builds and `test_client.py` passes.

## Boost.Asio vs Standalone Asio

Both are supported. The skill includes a portability shim pattern:

```cpp
#ifdef USE_STANDALONE_ASIO
  #include <asio.hpp>
  namespace net = asio;
#else
  #include <boost/asio.hpp>
  namespace net = boost::asio;
#endif
```

## Official Documentation

- Boost.Asio: https://www.boost.org/doc/libs/latest/doc/html/boost_asio.html
- Standalone Asio: https://think-async.com/Asio/
- Asio C++ Library (author's site): https://think-async.com/Asio/asio-1.30.2/doc/

## License

MIT
