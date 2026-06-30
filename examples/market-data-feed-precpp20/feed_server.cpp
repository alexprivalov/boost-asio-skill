// Market data feed server — pre-C++20 callback style (Boost.Asio, header-only).
// Built strictly per the boost-asio-pro skill's "Before Coroutines (pre-C++20)" section:
//   - callback full-duplex + write queue          (skill lines 530-569)
//   - watchdog idle timeout instead of `||` race  (skill lines 595-605)
//   - length-prefixed binary framing via async_read (skill lines 470-487)
//   - signal_set graceful shutdown                (skill lines 492-498)
//   - per-connection strand                       (skill "Strands" + callback class)
//
// No C++20 coroutine features used: no co_await/awaitable/co_spawn/use_awaitable/
// as_tuple/awaitable_operators/co_composed.

#include <boost/asio.hpp>


#include <array>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using boost::system::error_code;

// Frame types
enum : std::uint8_t {
    // client -> server
    SUBSCRIBE   = 0x01,
    UNSUBSCRIBE = 0x02,
    PING        = 0x03,
    // server -> client
    TICK        = 0x10,
    PONG        = 0x11,
    ERROR_FRAME = 0x12,
};

// Portable big-endian (network order) 32-bit codec — no <arpa/inet.h> / <winsock2.h>,
// so this builds unchanged on Linux, macOS, and Windows/MSVC.
static void put_be32(unsigned char* p, std::uint32_t v) {
    p[0] = static_cast<unsigned char>(v >> 24);
    p[1] = static_cast<unsigned char>(v >> 16);
    p[2] = static_cast<unsigned char>(v >> 8);
    p[3] = static_cast<unsigned char>(v);
}
static std::uint32_t get_be32(const unsigned char* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16)
         | (std::uint32_t(p[2]) << 8)  |  std::uint32_t(p[3]);
}

// Build a wire frame: [4-byte BE length N][1-byte type][N-1 payload].
// N counts the type byte plus the payload.
static std::string make_frame(std::uint8_t type, const std::string& payload) {
    std::uint32_t n = static_cast<std::uint32_t>(1 + payload.size());
    std::string frame;
    frame.resize(4);
    put_be32(reinterpret_cast<unsigned char*>(&frame[0]), n);
    frame.push_back(static_cast<char>(type));
    frame.append(payload);
    return frame;
}

class connection : public std::enable_shared_from_this<connection> {
    tcp::socket socket_;
    asio::strand<asio::any_io_executor> strand_;   // tcp::socket's executor is any_io_executor
    asio::steady_timer idle_timer_;

    std::deque<std::string> outbox_;
    bool writing_ = false;

    // framing read state
    unsigned char len_buf_[4] = {};  // 4-byte big-endian length, decoded with get_be32
    std::string body_;               // type byte + payload

    // one ticker timer per active subscription
    std::map<std::string, std::shared_ptr<asio::steady_timer>> tickers_;
    double price_ = 100.0;

public:
    explicit connection(tcp::socket s)
        : socket_(std::move(s)),
          strand_(asio::make_strand(socket_.get_executor())),
          idle_timer_(socket_.get_executor()) {}

    void start() {
        arm_timeout();
        do_read_header();
    }

private:
    // ---- write queue (skill callback full-duplex pattern) ----
    // Call on the strand only.
    void send(std::string frame) {
        outbox_.push_back(std::move(frame));
        if (!writing_) do_write();
    }

    void do_write() {                        // at most one async_write in flight
        writing_ = true;
        auto self = shared_from_this();
        asio::async_write(socket_, asio::buffer(outbox_.front()),
            asio::bind_executor(strand_,
                [this, self](error_code ec, std::size_t) {
                    if (ec) { writing_ = false; return; }
                    outbox_.pop_front();
                    if (!outbox_.empty()) do_write();
                    else writing_ = false;
                }));
    }

    // ---- framing: read 4-byte length fully, then body fully (composed reads) ----
    void do_read_header() {
        auto self = shared_from_this();
        asio::async_read(socket_, asio::buffer(len_buf_, sizeof len_buf_),
            asio::bind_executor(strand_,
                [this, self](error_code ec, std::size_t) {
                    if (ec) return;                 // self drops → socket closes
                    on_frame_received();            // any frame resets idle timer
                    std::uint32_t n = get_be32(len_buf_);
                    if (n == 0 || n > (16u * 1024u * 1024u)) {
                        send(make_frame(ERROR_FRAME, "bad frame length"));
                        return;                     // drop connection
                    }
                    body_.assign(n, '\0');
                    do_read_body();
                }));
    }

    void do_read_body() {
        auto self = shared_from_this();
        asio::async_read(socket_, asio::buffer(&body_[0], body_.size()),
            asio::bind_executor(strand_,
                [this, self](error_code ec, std::size_t) {
                    if (ec) return;
                    handle_frame();
                    do_read_header();
                }));
    }

    void handle_frame() {
        std::uint8_t type = static_cast<std::uint8_t>(body_[0]);
        std::string payload = body_.substr(1);
        switch (type) {
            case SUBSCRIBE:   subscribe(payload);   break;
            case UNSUBSCRIBE: unsubscribe(payload); break;
            case PING:        send(make_frame(PONG, "")); break;
            default:          send(make_frame(ERROR_FRAME, "unknown type")); break;
        }
    }

    // ---- per-symbol ticker: push a TICK every 250ms while subscribed ----
    void subscribe(const std::string& symbol) {
        if (symbol.empty()) { send(make_frame(ERROR_FRAME, "empty symbol")); return; }
        if (tickers_.count(symbol)) return;          // already subscribed
        auto timer = std::make_shared<asio::steady_timer>(socket_.get_executor());
        tickers_[symbol] = timer;
        schedule_tick(symbol, timer);
    }

    void unsubscribe(const std::string& symbol) {
        auto it = tickers_.find(symbol);
        if (it != tickers_.end()) {
            it->second->cancel();                    // stops the pending wait
            tickers_.erase(it);
        }
    }

    void schedule_tick(const std::string& symbol,
                       std::shared_ptr<asio::steady_timer> timer) {
        timer->expires_after(std::chrono::milliseconds(250));
        auto self = shared_from_this();
        timer->async_wait(asio::bind_executor(strand_,
            [this, self, symbol, timer](error_code ec) {
                if (ec) return;                      // cancelled (unsubscribe/close)
                if (!tickers_.count(symbol)) return; // defensive: gone
                price_ += 0.25;
                char buf[64];
                std::snprintf(buf, sizeof buf, "%s:%.2f", symbol.c_str(), price_);
                send(make_frame(TICK, buf));
                schedule_tick(symbol, timer);        // reschedule while subscribed
            }));
    }

    // ---- watchdog idle timeout (skill: arm_timeout, re-arm on each frame) ----
    void arm_timeout() {
        idle_timer_.expires_after(std::chrono::seconds(30));
        auto self = shared_from_this();
        idle_timer_.async_wait(asio::bind_executor(strand_,
            [this, self](error_code ec) {
                if (!ec) {                           // fired → drop
                    socket_.close();
                    for (auto& kv : tickers_) kv.second->cancel();
                    tickers_.clear();
                }
                // ec set (e.g. operation_aborted from re-arm) → ignore
            }));
    }

    void on_frame_received() { arm_timeout(); }      // re-arm on every received frame
};

class server {
    tcp::acceptor acceptor_;
public:
    server(asio::io_context& io, unsigned short port)
        : acceptor_(io, {tcp::v4(), port}) {
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        do_accept();
    }
    void close() { acceptor_.close(); }
private:
    void do_accept() {
        acceptor_.async_accept(asio::make_strand(acceptor_.get_executor()),
            [this](error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<connection>(std::move(socket))->start();
                }
                if (acceptor_.is_open()) do_accept();
            });
    }
};

int main() {
    try {
        asio::io_context io(1);
        server srv(io, 9092);

        asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&](const error_code&, int) {
            srv.close();    // stop accepting; let in-flight sessions drain
            io.stop();
        });

        std::cout << "feed_server listening on 9092\n";
        io.run();
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
