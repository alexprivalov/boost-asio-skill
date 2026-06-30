// Market data feed server — full-duplex custom binary framing.
//
// Wire frame (all integers big-endian):
//   [4 bytes: payload length N][1 byte: message type][N-1 bytes: payload]
//
// client->server:  0x01 SUBSCRIBE, 0x02 UNSUBSCRIBE, 0x03 PING
// server->client:  0x10 TICK, 0x11 PONG, 0x12 ERROR
//
// Built following the boost-asio-pro skill patterns: C++20 coroutines,
// per-connection strand for full-duplex serialization, per-connection write
// queue, composed reads (async_read), idle timeout via awaitable_operators,
// graceful SIGINT shutdown.

#define BOOST_ASIO_HEADER_ONLY

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using error_code = boost::system::error_code;
using namespace std::chrono_literals;
using namespace asio::experimental::awaitable_operators;

namespace {

// Message type constants.
constexpr std::uint8_t MSG_SUBSCRIBE   = 0x01;
constexpr std::uint8_t MSG_UNSUBSCRIBE = 0x02;
constexpr std::uint8_t MSG_PING        = 0x03;
constexpr std::uint8_t MSG_TICK        = 0x10;
constexpr std::uint8_t MSG_PONG        = 0x11;
constexpr std::uint8_t MSG_ERROR       = 0x12;

constexpr auto TICK_INTERVAL  = 250ms;
constexpr auto IDLE_TIMEOUT   = 30s;
constexpr std::uint32_t MAX_FRAME = 64 * 1024;  // sanity cap on payload length

// Encode a frame: [4-byte BE length=N][1-byte type][payload (N-1 bytes)].
std::vector<unsigned char> encode_frame(std::uint8_t type, std::string_view payload) {
    const std::uint32_t n = static_cast<std::uint32_t>(payload.size() + 1);
    std::vector<unsigned char> out;
    out.reserve(4 + n);
    out.push_back(static_cast<unsigned char>((n >> 24) & 0xFF));
    out.push_back(static_cast<unsigned char>((n >> 16) & 0xFF));
    out.push_back(static_cast<unsigned char>((n >> 8) & 0xFF));
    out.push_back(static_cast<unsigned char>(n & 0xFF));
    out.push_back(static_cast<unsigned char>(type));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// One connection. Holds the socket, its dedicated strand, the write queue, and
// subscription state. Lifetime managed by shared_ptr because the tick-push and
// read coroutines both reference it concurrently.
class Connection : public std::enable_shared_from_this<Connection> {
public:
    explicit Connection(tcp::socket socket)
        : socket_(std::move(socket)),
          strand_(socket_.get_executor()),
          idle_timer_(strand_) {}

    void start() {
        auto self = shared_from_this();
        // Read loop + idle timeout run as one logical chain. Tick pushes are
        // spawned per-subscription. All run on the same strand.
        idle_timer_.expires_after(IDLE_TIMEOUT);
        asio::co_spawn(strand_, run(), [self](std::exception_ptr ep) {
            if (ep) {
                try { std::rethrow_exception(ep); }
                catch (const std::exception& e) {
                    std::cerr << "session ended: " << e.what() << "\n";
                }
            }
        });
    }

private:
    // Main session: read frames, dispatch, while idle timer races.
    asio::awaitable<void> run() {
        try {
            co_await (read_loop() || idle_watch());
        } catch (const std::exception& e) {
            std::cerr << "run error: " << e.what() << "\n";
        }
        close();
    }

    asio::awaitable<void> idle_watch() {
        // Re-arm whenever a frame arrives (read_loop refreshes the timer).
        for (;;) {
            error_code ec;
            co_await idle_timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
            if (ec == asio::error::operation_aborted) {
                // Timer was re-armed; keep watching.
                continue;
            }
            // Genuine expiry.
            std::cerr << "idle timeout, dropping connection\n";
            co_return;
        }
    }

    asio::awaitable<void> read_loop() {
        for (;;) {
            // Composed read of the 4-byte length prefix (handles short reads).
            unsigned char lenbuf[4];
            co_await asio::async_read(socket_, asio::buffer(lenbuf), asio::use_awaitable);

            std::uint32_t n = (std::uint32_t(lenbuf[0]) << 24) |
                              (std::uint32_t(lenbuf[1]) << 16) |
                              (std::uint32_t(lenbuf[2]) << 8) |
                              (std::uint32_t(lenbuf[3]));

            if (n == 0 || n > MAX_FRAME) {
                enqueue_write(encode_frame(MSG_ERROR, "bad frame length"));
                co_return;  // protocol violation -> drop
            }

            // Frame received -> refresh idle deadline.
            idle_timer_.expires_after(IDLE_TIMEOUT);

            // Read the rest: 1 type byte + (n-1) payload bytes.
            std::vector<unsigned char> body(n);
            co_await asio::async_read(socket_, asio::buffer(body), asio::use_awaitable);

            std::uint8_t type = body[0];
            std::string payload(reinterpret_cast<const char*>(body.data() + 1), n - 1);

            switch (type) {
                case MSG_SUBSCRIBE:   handle_subscribe(payload);   break;
                case MSG_UNSUBSCRIBE: handle_unsubscribe(payload); break;
                case MSG_PING:        enqueue_write(encode_frame(MSG_PONG, "")); break;
                default:
                    enqueue_write(encode_frame(MSG_ERROR, "unknown message type"));
                    break;
            }
        }
    }

    void handle_subscribe(const std::string& symbol) {
        if (symbol.empty()) {
            enqueue_write(encode_frame(MSG_ERROR, "empty symbol"));
            return;
        }
        if (subscriptions_.count(symbol)) {
            return;  // already subscribed, idempotent
        }
        subscriptions_.insert(symbol);
        // Spawn a per-symbol tick pusher on the same strand.
        auto self = shared_from_this();
        asio::co_spawn(strand_, tick_pusher(symbol), asio::detached);
    }

    void handle_unsubscribe(const std::string& symbol) {
        subscriptions_.erase(symbol);
        // tick_pusher notices the symbol is gone on its next wake.
    }

    asio::awaitable<void> tick_pusher(std::string symbol) {
        asio::steady_timer timer(strand_);
        double price = 100.0;
        while (subscriptions_.count(symbol)) {
            timer.expires_after(TICK_INTERVAL);
            error_code ec;
            co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
            if (ec || !socket_.is_open()) co_return;
            if (!subscriptions_.count(symbol)) co_return;

            // Mutate price a little; format as ASCII "SYMBOL:price".
            price += 0.13;
            char buf[64];
            int len = std::snprintf(buf, sizeof(buf), "%s:%.2f", symbol.c_str(), price);
            enqueue_write(encode_frame(MSG_TICK, std::string_view(buf, len)));
        }
    }

    // Per-connection write queue: append frame, kick the writer if idle.
    void enqueue_write(std::vector<unsigned char> frame) {
        write_queue_.push_back(std::move(frame));
        if (!writing_) {
            writing_ = true;
            auto self = shared_from_this();
            asio::co_spawn(strand_, writer(), asio::detached);
        }
    }

    asio::awaitable<void> writer() {
        while (!write_queue_.empty()) {
            auto frame = std::move(write_queue_.front());
            write_queue_.pop_front();
            error_code ec;
            co_await asio::async_write(socket_, asio::buffer(frame),
                                       asio::redirect_error(asio::use_awaitable, ec));
            if (ec) {
                close();
                break;
            }
        }
        writing_ = false;
    }

    void close() {
        error_code ec;
        socket_.close(ec);
        idle_timer_.cancel();
    }

    tcp::socket socket_;
    asio::strand<tcp::socket::executor_type> strand_;
    asio::steady_timer idle_timer_;
    std::deque<std::vector<unsigned char>> write_queue_;
    bool writing_ = false;
    std::unordered_set<std::string> subscriptions_;
};

asio::awaitable<void> listener(tcp::acceptor acceptor) {
    for (;;) {
        // Accept onto a fresh strand so each connection's I/O is serialized.
        auto socket = co_await acceptor.async_accept(
            asio::make_strand(acceptor.get_executor()), asio::use_awaitable);
        std::make_shared<Connection>(std::move(socket))->start();
    }
}

}  // namespace

int main() {
    try {
        asio::io_context io(1);

        tcp::acceptor acceptor(io, {tcp::v4(), 9090});
        acceptor.set_option(tcp::acceptor::reuse_address(true));

        asio::co_spawn(io, listener(std::move(acceptor)), [](std::exception_ptr ep) {
            if (ep) {
                try { std::rethrow_exception(ep); }
                catch (const std::exception& e) {
                    std::cerr << "listener error: " << e.what() << "\n";
                }
            }
        });

        // Graceful shutdown on SIGINT.
        asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&](const error_code&, int) {
            std::cerr << "shutting down\n";
            io.stop();
        });

        std::cout << "feed_server listening on :9090\n";
        io.run();
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
