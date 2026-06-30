// Market data feed server — CLASSIC / conservative Boost.Asio style.
//
// Same full-duplex framed protocol as ../market-data-feed-precpp20/, but written
// to compile on VERY OLD Boost (tested back to Boost 1.62 / Debian 9, 2017) as
// well as current Boost (1.90). It deliberately avoids everything added in the
// io_context era (1.66+):
//   - no io_context        -> boost::asio::io_service (aliased below)
//   - no make_strand       -> io_service::strand + strand.wrap(handler)
//   - no bind_executor     -> strand.wrap(handler)
//   - no any_io_executor   -> plain socket / classic strand
//   - no expires_after     -> expires_from_now (aliased below)
//   - no co_await/awaitable, no awaitable_operators, no as_tuple
//   - links Boost.System   -> error_code is not header-only on old Boost
//
// The ONLY version-dependent bits are the io_service/io_context name and the
// timer-expiry call; both are isolated in the small compat shim below.

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>   // not pulled in by <boost/asio.hpp> on old Boost (1.65)
#include <boost/version.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using boost::system::error_code;

// ---- compat shim: the only two things that differ across Boost 1.62 .. 1.90 ----
#if BOOST_VERSION >= 106600
using io_service_t = asio::io_context;          // io_context is the modern name
#else
using io_service_t = asio::io_service;          // pre-1.66: io_service
#endif

template <class Timer, class Rep, class Period>
static void timer_expires_in(Timer& t, std::chrono::duration<Rep, Period> d) {
#if BOOST_VERSION >= 106600
    t.expires_after(d);                          // modern
#else
    t.expires_from_now(d);                       // pre-1.66
#endif
}

// ---- portable big-endian codec (no <arpa/inet.h>; works on every platform) ----
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

enum : std::uint8_t {
    SUBSCRIBE = 0x01, UNSUBSCRIBE = 0x02, PING = 0x03,
    TICK = 0x10, PONG = 0x11, ERROR_FRAME = 0x12,
};

// Frame: [4-byte BE length N][1-byte type][N-1 payload]. N counts type + payload.
static std::string make_frame(std::uint8_t type, const std::string& payload) {
    std::uint32_t n = static_cast<std::uint32_t>(1 + payload.size());
    std::string frame;
    frame.resize(4);
    put_be32(reinterpret_cast<unsigned char*>(&frame[0]), n);
    frame.push_back(static_cast<char>(type));
    frame.append(payload);
    return frame;
}

class session : public std::enable_shared_from_this<session> {
public:
    // The strand and timer need the io_service&, so pass it in explicitly
    // (classic strand has no executor-based constructor on old Boost).
    session(io_service_t& io, tcp::socket socket)
        : io_(io), socket_(std::move(socket)), strand_(io), idle_timer_(io) {}

    void start() {
        arm_idle_timeout();
        do_read_header();
    }

private:
    // ---- write queue: one async_write in flight, so frames never interleave ----
    void send(const std::string& frame) {
        outbox_.push_back(frame);
        if (!writing_) do_write();
    }
    void do_write() {
        writing_ = true;
        std::shared_ptr<session> self = shared_from_this();
        asio::async_write(socket_, asio::buffer(outbox_.front()),
            strand_.wrap([this, self](const error_code& ec, std::size_t) {
                if (ec) { writing_ = false; return; }
                outbox_.pop_front();
                if (!outbox_.empty()) do_write();
                else writing_ = false;
            }));
    }

    // ---- framing: read 4-byte length fully, then body fully ----
    void do_read_header() {
        std::shared_ptr<session> self = shared_from_this();
        asio::async_read(socket_, asio::buffer(len_buf_, sizeof len_buf_),
            strand_.wrap([this, self](const error_code& ec, std::size_t) {
                if (ec) return;                  // self drops -> socket closes
                arm_idle_timeout();              // any frame resets the idle timer
                std::uint32_t n = get_be32(len_buf_);
                if (n == 0 || n > 16u * 1024u * 1024u) { send(make_frame(ERROR_FRAME, "bad length")); return; }
                body_.assign(n, '\0');
                do_read_body();
            }));
    }
    void do_read_body() {
        std::shared_ptr<session> self = shared_from_this();
        asio::async_read(socket_, asio::buffer(&body_[0], body_.size()),
            strand_.wrap([this, self](const error_code& ec, std::size_t) {
                if (ec) return;
                handle_frame();
                do_read_header();
            }));
    }

    void handle_frame() {
        std::uint8_t type = static_cast<std::uint8_t>(body_[0]);
        std::string payload = body_.substr(1);
        switch (type) {
            case PING: send(make_frame(PONG, "")); break;
            case SUBSCRIBE:   subscribe(payload);   break;
            case UNSUBSCRIBE: unsubscribe(payload); break;
            default: send(make_frame(ERROR_FRAME, "unknown type"));
        }
    }

    // ---- per-symbol ticker: a self-rescheduling timer, cancelled on unsubscribe ----
    void subscribe(const std::string& symbol) {
        if (tickers_.count(symbol)) return;
        std::shared_ptr<asio::steady_timer> t(new asio::steady_timer(io_));
        tickers_[symbol] = t;
        schedule_tick(symbol, t);
    }
    void unsubscribe(const std::string& symbol) {
        std::map<std::string, std::shared_ptr<asio::steady_timer> >::iterator it = tickers_.find(symbol);
        if (it == tickers_.end()) return;
        it->second->cancel();           // no-arg cancel() works on old and modern Boost
        tickers_.erase(it);
    }
    void schedule_tick(const std::string& symbol, std::shared_ptr<asio::steady_timer> t) {
        timer_expires_in(*t, std::chrono::milliseconds(250));
        std::shared_ptr<session> self = shared_from_this();
        t->async_wait(strand_.wrap([this, self, symbol, t](const error_code& ec) {
            if (ec) return;                      // cancelled -> stop
            if (!tickers_.count(symbol)) return;
            price_ += 0.25;
            char buf[32];
            std::snprintf(buf, sizeof buf, "%s:%.2f", symbol.c_str(), price_);
            send(make_frame(TICK, buf));
            schedule_tick(symbol, t);            // reschedule
        }));
    }

    // ---- idle timeout watchdog: re-arm on each frame, close on real expiry ----
    void arm_idle_timeout() {
        timer_expires_in(idle_timer_, std::chrono::seconds(30));
        std::shared_ptr<session> self = shared_from_this();
        idle_timer_.async_wait(strand_.wrap([this, self](const error_code& ec) {
            if (!ec) {                            // fired (not cancelled by re-arm)
                error_code ignore;
                socket_.close(ignore);
            }
        }));
    }

    io_service_t& io_;                            // kept to construct per-symbol timers
    tcp::socket socket_;
    io_service_t::strand strand_;                 // classic strand (wrap() handlers onto it)
    asio::steady_timer idle_timer_;
    std::deque<std::string> outbox_;
    bool writing_ = false;
    unsigned char len_buf_[4] = {};
    std::string body_;
    std::map<std::string, std::shared_ptr<asio::steady_timer> > tickers_;
    double price_ = 100.0;
};

class server {
public:
    server(io_service_t& io, unsigned short port)
        : io_(io), acceptor_(io, tcp::endpoint(tcp::v4(), port)), socket_(io) {
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        do_accept();
    }
private:
    void do_accept() {
        // Classic accept-into-a-member-socket form (no move-return overload pre-1.66).
        acceptor_.async_accept(socket_, [this](const error_code& ec) {
            if (!ec) std::make_shared<session>(io_, std::move(socket_))->start();
            if (acceptor_.is_open()) do_accept();
        });
    }
    io_service_t& io_;
    tcp::acceptor acceptor_;
    tcp::socket socket_;
};

int main() {
    try {
        io_service_t io;
        server srv(io, 9093);
        std::cout << "classic feed_server listening on 9093" << std::endl;

        asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&io](const error_code&, int) { io.stop(); });

        // A small thread pool — this is why every handler is wrapped on the strand.
        unsigned n = std::thread::hardware_concurrency();
        if (n == 0) n = 2;
        std::vector<std::thread> pool;
        for (unsigned i = 1; i < n; ++i) pool.emplace_back([&io] { io.run(); });
        io.run();
        for (std::thread& t : pool) t.join();
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
