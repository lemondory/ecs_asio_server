#include "client/game_bot.hpp"
#include "common/protocol/protocol_player.hpp"
#include "common/protocol/protocol_map.hpp"
#include <iostream>
#include <chrono>
#include <cstring>

using tcp = asio::ip::tcp;
using namespace lemondory::network::frame_codec;
using namespace lemondory::shared;

namespace lemondory::client {

GameBot::GameBot(std::string player_name, std::string host, int port)
    : player_name_(std::move(player_name))
    , host_(std::move(host))
    , port_(port)
    , socket_(io_) {}

GameBot::~GameBot() {
    disconnect();
}

std::string GameBot::authenticate() {
    // Phase 1: no auth server, token is empty.
    // Phase 3: replace with http_post(auth_host, "/auth/login", credentials).
    return "";
}

bool GameBot::connect() {
    session_token_ = authenticate();

    asio::error_code ec;
    tcp::resolver resolver(io_);
    auto endpoints = resolver.resolve(host_, std::to_string(port_), ec);
    if (ec) {
        std::cerr << "[bot] resolve: " << ec.message() << "\n";
        return false;
    }

    asio::connect(socket_, endpoints, ec);
    if (ec) {
        std::cerr << "[bot] connect: " << ec.message() << "\n";
        return false;
    }

    std::cout << "[bot] connected to " << host_ << ":" << port_ << "\n";

    running_ = true;
    recv_thread_ = std::thread([this] { recv_loop(); });

    send_login();
    return true;
}

void GameBot::run(int move_count) {
    // 맵 진입 완료까지 최대 3초 대기 (100ms × 30)
    for (int i = 0; i < 30 && running_ && !in_map_; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (!in_map_) {
        std::cerr << "[" << player_name_ << "] timeout waiting for map entry\n";
        return;
    }

    for (int i = 0; i < move_count && running_; ++i) {
        // 셀 크기(100) 경계를 넘나들도록 이동 — AOI 브로드캐스트 트리거
        float x = static_cast<float>(i * 30);
        float z = static_cast<float>(i * 20);
        send_move(x, 0.f, z);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));

        if (i % 3 == 0)
            send_chat("step=" + std::to_string(i) + " pos=(" +
                      std::to_string(static_cast<int>(x)) + "," +
                      std::to_string(static_cast<int>(z)) + ")");
    }

    send_ping();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

void GameBot::disconnect() {
    running_ = false;
    asio::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_both, ec);
    socket_.close(ec);
    if (recv_thread_.joinable())
        recv_thread_.join();
}

void GameBot::send_frame(std::uint16_t cmd, const char* data, std::size_t size) {
    auto pkt = pack(cmd, 0, k_proto_ver, 0, std::string_view{data, size});
    std::lock_guard<std::mutex> lk(send_mutex_);
    asio::error_code ec;
    asio::write(socket_, asio::buffer(pkt), ec);
    if (ec && running_)
        std::cerr << "[bot] send cmd=" << cmd << " error: " << ec.message() << "\n";
}

void GameBot::send_login() {
    login_req req{player_name_, session_token_};
    auto payload = req.serialize();
    send_frame(1001, payload.data(), payload.size());
    std::cout << "[bot] → LOGIN player=" << player_name_ << "\n";
}

void GameBot::send_map_enter(int map_id, float x, float y, float z) {
    using namespace lemondory::shared;
    map_enter_req req;
    req.map_id = static_cast<std::uint32_t>(map_id);
    req.x = x; req.y = y; req.z = z;
    req.timestamp = 0;
    auto payload = req.serialize();
    send_frame(1005, payload.data(), payload.size());
    std::cout << "[" << player_name_ << "] → MAP_ENTER map=" << map_id << "\n";
}

void GameBot::send_move(float x, float y, float z) {
    lemondory::shared::player_move_req req{x, y, z, 0.f, 0};
    auto payload = req.serialize();
    send_frame(1002, payload.data(), payload.size());
    std::cout << "[bot] → MOVE (" << x << "," << y << "," << z << ")\n";
}

void GameBot::send_chat(const std::string& msg) {
    send_frame(1003, msg.data(), msg.size());
    std::cout << "[bot] → CHAT \"" << msg << "\"\n";
}

void GameBot::send_ping() {
    send_frame(1, nullptr, 0);
    std::cout << "[bot] → PING\n";
}

void GameBot::recv_loop() {
    char buf[4096];

    while (running_) {
        asio::error_code ec;
        std::size_t n = socket_.read_some(asio::buffer(buf), ec);
        if (ec) break;

        acc_.insert(acc_.end(), buf, buf + n);

        for (;;) {
            header_v1 hdr{};
            std::string_view payload;
            bool crc_ok = false;
            if (!try_parse_one(acc_, acc_offset_, hdr, payload, crc_ok)) break;
            if (!crc_ok) {
                std::cerr << "[bot] CRC mismatch cmd=" << hdr.cmd << "\n";
                continue;
            }
            on_frame(hdr.cmd, payload);
        }

        if (acc_offset_ > acc_.size() / 2) {
            acc_.erase(acc_.begin(), acc_.begin() + static_cast<std::ptrdiff_t>(acc_offset_));
            acc_offset_ = 0;
        }
    }
}

void GameBot::on_frame(std::uint16_t cmd, std::string_view payload) {
    using namespace lemondory::shared;
    const char* p   = payload.data();
    const char* end = p + payload.size();

    switch (cmd) {
    case 1: // PING → PONG
        send_frame(2, nullptr, 0);
        break;

    case 2: // PONG
        std::cout << "[" << player_name_ << "] ← PONG\n";
        break;

    case 1001: { // LOGIN_RES
        login_res res{};
        if (login_res::parse(p, end, res) && res.success) {
            spawn_map_id_ = static_cast<int>(res.map_id);
            std::cout << "[" << player_name_ << "] ← LOGIN_RES ok"
                      << " player_id=" << res.player_id
                      << " spawn_map=" << spawn_map_id_ << "\n";
            send_map_enter(spawn_map_id_);
        } else {
            std::cout << "[" << player_name_ << "] ← LOGIN_RES failed\n";
        }
        break;
    }

    case 1002: { // PLAYER_MOVE_RES — 인접 플레이어 이동 AOI 브로드캐스트
        player_move_res res{};
        if (player_move_res::parse(p, end, res)) {
            std::cout << "[" << player_name_ << "] ← AOI_MOVE player=" << res.player_id
                      << " pos=(" << res.x << "," << res.y << "," << res.z << ")\n";
        }
        break;
    }

    case 1003: { // CHAT broadcast
        std::string msg(p, end);
        std::cout << "[" << player_name_ << "] ← CHAT \"" << msg << "\"\n";
        break;
    }

    case 1005: { // MAP_ENTER_RES
        map_enter_res res{};
        if (map_enter_res::parse(p, end, res)) {
            if (res.success) {
                std::cout << "[" << player_name_ << "] ← MAP_ENTER_RES ok"
                          << " map=" << res.map_id << "(" << res.map_name << ")"
                          << " pos=(" << res.x << "," << res.y << "," << res.z << ")\n";
                in_map_ = true;
            } else {
                std::cout << "[" << player_name_ << "] ← MAP_ENTER_RES failed: " << res.message << "\n";
            }
        }
        break;
    }

    default:
        std::cout << "[" << player_name_ << "] ← cmd=" << cmd
                  << " size=" << payload.size() << "\n";
        break;
    }
}

} // namespace lemondory::client
