// asio_server.cpp
#include "asio_server.hpp"
#include "asio_channel.hpp"
#include "common/network/socket_channel_base.hpp"
#include "common/network/socket_platform.hpp"
#include "common/log.hpp"

namespace lemondory::network {

asio_server::asio_server(asio::io_context& io_context)
    : io_context_(io_context), acceptor_(io_context) {}

asio_server::~asio_server() {
    stop();
}

bool asio_server::init(net_handler* handler, const socket_option& option,
                       int /*init_channel_count*/, int /*io_thread_count*/) {
    handler_ = handler;
    socket_option_ = option;
    return true;
}

bool asio_server::listen(const std::string& ip_address, int port) {
    asio::ip::tcp::endpoint endpoint(asio::ip::make_address(ip_address), port);
    asio::error_code ec;

    if (acceptor_.is_open()) {
        acceptor_.close(ec);
    }

    acceptor_ = asio::ip::tcp::acceptor(io_context_);
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) { LOGE("[server] open: {}", ec.message()); return false; }

    acceptor_.set_option(asio::socket_base::reuse_address(socket_option_.reuse_address()), ec);
    if (ec) { LOGE("[server] reuse_address: {}", ec.message()); return false; }

    acceptor_.bind(endpoint, ec);
    if (ec) {
        LOGE("[server] bind {}:{} failed: {}", ip_address, port, ec.message());
        return false;
    }

    acceptor_.listen(socket_option_.backlog(), ec);
    if (ec) { LOGE("[server] listen: {}", ec.message()); return false; }

    do_accept();
    return true;
}

bool asio_server::stop() {
    asio::error_code ec;
    acceptor_.close(ec);
    return !ec;
}

void asio_server::release() {
    stop();
    handler_ = nullptr;
}

void asio_server::set_net_handler(net_handler* handler) { handler_ = handler; }
net_handler* asio_server::get_net_handler() { return handler_; }

void asio_server::do_accept()
{
    acceptor_.async_accept(
        [this](asio::error_code ec, asio::ip::tcp::socket socket)
        {
            if (ec) {
                LOGE("[server] accept error: {}", ec.message());
                do_accept();
                return;
            }

            auto channel = std::make_shared<asio_channel>(std::move(socket), handler_);

            // 프레임 파싱 완료 시 상위 콜백으로 중계
            channel->set_on_frame([this](int channel_id, std::uint16_t cmd,
                                         const char* payload, std::size_t size) {
                if (on_frame_) on_frame_(channel_id, cmd, payload, size);
            });

            // 채널 ID 발급 및 맵 등록
            const int id = next_channel_id_.fetch_add(1, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lk(channel_mutex_);
                channels_[id] = channel;
            }
            channel->set_channel_id(id);

            // 채널 종료 시 channels_ 에서 자동 제거
            channel->set_close_callback([this, id](const void*, close_function) {
                std::lock_guard<std::mutex> lk(channel_mutex_);
                channels_.erase(id);
                LOGD("[server] channel {} removed", id);
            });

            LOGI("[server] accepted channel id={}", id);
            channel->start();

            do_accept();
        });
}

} // namespace lemondory::network
