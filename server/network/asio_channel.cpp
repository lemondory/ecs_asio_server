// asio_channel.cpp
#include "asio_channel.hpp"
#include "asio_server.hpp"
#include "common/network/net_handler.hpp"
#include "common/network/frame_codec.hpp"
#include "common/log.hpp"

namespace lemondory::network
{

asio_channel::asio_channel(asio::ip::tcp::socket socket, net_handler* handler)
    : socket_(std::move(socket))
    , read_buffer_(1024)
    , idle_timer_(socket_.get_executor())
    , ping_timer_(socket_.get_executor())
    , stats_timer_(socket_.get_executor())
{
    handler_ = handler;
    frame_acc_.reserve(64 * 1024);
}

void asio_channel::start()
{
    closed_.store(false, std::memory_order_relaxed);
    writing_.store(false, std::memory_order_relaxed);

    frame_acc_.clear();
    frame_acc_.reserve(64 * 1024);
    frame_acc_offset_ = 0;

    current_queue_bytes.store(0, std::memory_order_relaxed);
    peak_queued.store(0, std::memory_order_relaxed);
    enq_msgs.store(0, std::memory_order_relaxed);
    deq_msgs.store(0, std::memory_order_relaxed);
    bytes_sent.store(0, std::memory_order_relaxed);

    const auto now = now_ms();
    last_recv_ms.store(now, std::memory_order_relaxed);
    last_send_ms.store(now, std::memory_order_relaxed);

    do_read();

    if (handler_)
        handler_->on_accept(this->get_channel_id(), this);

    arm_idle_timer();
    arm_ping_timer();
    arm_stats_timer();
}

// ---- send ----------------------------------------------------------------

void asio_channel::send(const char* data, int size)
{
    if (closed_ || size <= 0) return;

    std::vector<char> buffer(data, data + size);
    if (!enqueue_send_(std::move(buffer))) {
        LOGW("[channel] backpressure close id={} queued={}B limit={}B",
             get_channel_id(),
             current_queue_bytes.load(std::memory_order_relaxed), max_queue_bytes);
        force_closes.fetch_add(1, std::memory_order_relaxed);
        close(nullptr, close_function::force_closed);
        return;
    }

    bool expected = false;
    if (writing_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        do_write();
}

void asio_channel::recv(char* /*buffer*/, int /*size*/)
{
    do_read();
}

// ---- close ---------------------------------------------------------------

bool asio_channel::close(const void* error, close_function reason)
{
    if (closed_.exchange(true)) return false;

    if (reason == close_function::force_closed)
        force_closes.fetch_add(1, std::memory_order_relaxed);

    asio::post(socket_.get_executor(), [self = shared_from_this()]() {
        try {
            self->idle_timer_.cancel();
            self->ping_timer_.cancel();
            self->stats_timer_.cancel();

            std::error_code ignored;
            self->socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
            self->socket_.close(ignored);
        } catch (const std::exception& e) {
            LOGE("[channel] error during close id={} {}", self->get_channel_id(), e.what());
        }
    });

    try {
        on_close(error, reason);
    } catch (const std::exception& e) {
        LOGE("[channel] error in on_close id={} {}", get_channel_id(), e.what());
    }
    return true;
}

// ---- net_handler callbacks -----------------------------------------------

void asio_channel::on_connect(net_error error)
{
    if (handler_) handler_->on_connect(error);
}

void asio_channel::on_accept()
{
    if (handler_) handler_->on_accept(this->get_channel_id(), this);
}

void asio_channel::on_close(const void* error, close_function reason)
{
    if (handler_) handler_->on_close(this->get_channel_id(), error, reason);
    if (close_callback_) close_callback_(error, reason);
}

// ---- async read ----------------------------------------------------------

void asio_channel::do_read()
{
    auto self = shared_from_this();
    socket_.async_read_some(asio::buffer(read_buffer_),
        [self](std::error_code ec, std::size_t bytes_transferred) {
            if (ec) {
                LOGW("[channel] read error id={} ec={} {}", self->get_channel_id(), ec.value(), ec.message());
                self->close(&ec, close_function::read);
                return;
            }

            self->frame_acc_.insert(self->frame_acc_.end(),
                self->read_buffer_.begin(),
                self->read_buffer_.begin() + static_cast<std::ptrdiff_t>(bytes_transferred));

            self->parse_frames_();
            self->do_read();
        });
}

// ---- async write ---------------------------------------------------------

void asio_channel::do_write()
{
    std::vector<char> buffer;
    std::size_t batch_msgs = 0;

    if (!make_batch_(buffer, batch_msgs)) {
        writing_.store(false, std::memory_order_release);
        return;
    }

    auto self = shared_from_this();
    asio::async_write(socket_, asio::buffer(buffer),
        [self, buffer = std::move(buffer), batch_msgs](std::error_code ec, std::size_t n) {
            if (ec) {
                LOGW("[channel] write error id={} ec={} {}", self->get_channel_id(), ec.value(), ec.message());
                self->close(&ec, close_function::write);
                return;
            }

            self->current_queue_bytes.fetch_sub(n, std::memory_order_relaxed);
            self->deq_msgs.fetch_add(batch_msgs, std::memory_order_relaxed);
            self->bytes_sent.fetch_add(n, std::memory_order_relaxed);
            self->last_send_ms.store(now_ms(), std::memory_order_relaxed);

            self->writing_.store(false, std::memory_order_release);
            bool expected = false;
            if (!self->write_queue_.empty() &&
                self->writing_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                self->do_write();
            }
        });
}

bool asio_channel::enqueue_send_(std::vector<char> buffer)
{
    const std::size_t sz = buffer.size();
    if (sz == 0) return true;

    std::lock_guard<core::spin_lock> lk(write_lock_);

    const std::size_t cur = current_queue_bytes.load(std::memory_order_relaxed);
    if (cur + sz > max_queue_bytes) {
        drops.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    write_queue_.emplace(std::move(buffer));
    current_queue_bytes.store(cur + sz, std::memory_order_relaxed);
    enq_msgs.fetch_add(1, std::memory_order_relaxed);

    const std::size_t new_total = cur + sz;
    if (new_total > peak_queued.load(std::memory_order_relaxed))
        peak_queued.store(new_total, std::memory_order_relaxed);

    return true;
}

bool asio_channel::make_batch_(std::vector<char>& out, std::size_t& out_msgs)
{
    static constexpr std::size_t k_max_batch_bytes = 64 * 1024;
    static constexpr std::size_t k_max_batch_msgs  = 64;

    out.clear();
    out_msgs = 0;

    std::lock_guard<core::spin_lock> lk(write_lock_);
    if (write_queue_.empty()) return false;

    std::size_t acc = 0;
    while (!write_queue_.empty() && out_msgs < k_max_batch_msgs) {
        const std::vector<char>& front = write_queue_.front();
        if (acc + front.size() > k_max_batch_bytes) break;
        out.insert(out.end(), front.begin(), front.end());
        acc += front.size();
        write_queue_.pop();
        ++out_msgs;
    }
    return out_msgs > 0;
}

// ---- frame parsing -------------------------------------------------------

void asio_channel::parse_frames_()
{
    using namespace lemondory::network::frame_codec;

    for (;;) {
        header_v1 hdr{};
        std::string_view payload;
        bool crc_ok = false;

        if (!try_parse_one(frame_acc_, frame_acc_offset_, hdr, payload, crc_ok)) break;

        if (!crc_ok) {
            drops.fetch_add(1, std::memory_order_relaxed);
            LOGW("[channel] CRC mismatch id={} cmd={} — frame dropped", get_channel_id(), hdr.cmd);
            continue;
        }

        if (on_frame_)
            on_frame_(this->get_channel_id(), hdr.cmd, payload.data(), payload.size());
    }

    // 소비된 앞부분이 절반 이상이면 compact
    if (frame_acc_offset_ > frame_acc_.size() / 2) {
        frame_acc_.erase(frame_acc_.begin(),
                         frame_acc_.begin() + static_cast<std::ptrdiff_t>(frame_acc_offset_));
        frame_acc_offset_ = 0;
    }
}

// ---- timers --------------------------------------------------------------

void asio_channel::arm_idle_timer()
{
    idle_timer_.expires_after(idle_limit_);
    auto self = shared_from_this();
    idle_timer_.async_wait([self](const std::error_code& ec) {
        if (ec || self->closed_) return;

        const auto idle_ms = now_ms() - self->last_recv_ms.load(std::memory_order_relaxed);
        const auto limit_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(self->idle_limit_).count());

        if (idle_ms >= limit_ms) {
            LOGW("[channel] idle timeout id={} idle_ms={}", self->get_channel_id(), idle_ms);
            self->close(nullptr, close_function::timeout);
            return;
        }
        self->arm_idle_timer();
    });
}

void asio_channel::arm_ping_timer()
{
    ping_timer_.expires_after(ping_interval_);
    auto self = shared_from_this();
    ping_timer_.async_wait([self](const std::error_code& ec) {
        if (ec || self->closed_) return;
        self->send_ping();
        self->arm_ping_timer();
    });
}

void asio_channel::arm_stats_timer()
{
    stats_timer_.expires_after(stats_interval_);
    auto self = shared_from_this();
    stats_timer_.async_wait([self](const std::error_code& ec) {
        if (ec || self->closed_.load(std::memory_order_relaxed)) return;

        LOGD("[stats] id={} q={}B peak={}B enq={} deq={} drop={} fclose={} sent={}",
             self->get_channel_id(),
             self->current_queue_bytes.load(std::memory_order_relaxed),
             self->peak_queued.load(std::memory_order_relaxed),
             self->enq_msgs.load(std::memory_order_relaxed),
             self->deq_msgs.load(std::memory_order_relaxed),
             self->drops.load(std::memory_order_relaxed),
             self->force_closes.load(std::memory_order_relaxed),
             self->bytes_sent.load(std::memory_order_relaxed));

        self->arm_stats_timer();
    });
}

void asio_channel::send_ping()
{
    using namespace lemondory::network::frame_codec;
    auto pkt = pack(/*cmd*/1, /*flags*/0, k_proto_ver, /*seq*/0, /*payload*/std::string_view{});
    this->send(pkt.data(), static_cast<int>(pkt.size()));
}

} // namespace lemondory::network
