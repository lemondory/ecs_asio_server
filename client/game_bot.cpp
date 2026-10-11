#include "client/game_bot.hpp"
#include "common/protocol/protocol_player.hpp"
#include "common/protocol/protocol_map.hpp"
#include "common/protocol/protocol_combat.hpp"
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
        if (!alive_) break;

        // 셀 크기(100) 경계를 넘나들도록 이동 — AOI 브로드캐스트 트리거
        float x = static_cast<float>(i * 30);
        float z = static_cast<float>(i * 20);
        send_move(x, 0.f, z);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // 2스텝마다 주변 플레이어 공격
        if (i % 2 == 1) {
            std::uint32_t target = 0;
            {
                std::lock_guard<std::mutex> lk(nearby_mutex_);
                if (!nearby_players_.empty())
                    target = *nearby_players_.begin();
            }
            if (target != 0)
                send_attack(target);
        }

        if (i % 3 == 0)
            send_chat("step=" + std::to_string(i) + " pos=(" +
                      std::to_string(static_cast<int>(x)) + "," +
                      std::to_string(static_cast<int>(z)) + ")");

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
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

void GameBot::send_attack(std::uint32_t target_id, std::uint32_t skill_id) {
    using namespace lemondory::shared;
    auto now_ms = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    attack_req req{target_id, skill_id, 0.f, 0.f, 0.f, now_ms};
    auto payload = req.serialize();
    send_frame(1004, payload.data(), payload.size());
    std::cout << "[" << player_name_ << "] -> ATTACK target=" << target_id
              << " skill=" << skill_id << "\n";
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
            std::cout << "[" << player_name_ << "] <- AOI_MOVE player=" << res.player_id
                      << " pos=(" << res.x << "," << res.y << "," << res.z << ")\n";
            std::lock_guard<std::mutex> lk(nearby_mutex_);
            nearby_players_.insert(res.player_id);
        }
        break;
    }

    case 1003: { // CHAT broadcast
        std::string msg(p, end);
        std::cout << "[" << player_name_ << "] ← CHAT \"" << msg << "\"\n";
        break;
    }

    case 1004: { // ATTACK_RES — 내 공격 결과
        using namespace lemondory::shared;
        attack_res res{};
        if (attack_res::parse(p, end, res)) {
            std::cout << "[" << player_name_ << "] <- ATTACK_RES "
                      << (res.success ? "hit" : "miss")
                      << " dmg=" << res.damage
                      << " target_hp=" << res.target_remaining_hp << "\n";
            // 서버가 사망 상태로 판정 — login_res.player_id는 db_id라 1006의 channel_id와 비교 불가
            if (!res.success && res.message == "you are dead")
                alive_ = false;
        }
        break;
    }

    case 1008: { // DAMAGE_NOTIFY — AOI 범위 내 전투 브로드캐스트
        using namespace lemondory::shared;
        damage_notify notify{};
        if (damage_notify::parse(p, end, notify)) {
            std::cout << "[" << player_name_ << "] <- DAMAGE attacker=" << notify.attacker_id
                      << " target=" << notify.target_id
                      << " dmg=" << notify.damage
                      << " hp=" << notify.remaining_hp << "\n";
        }
        break;
    }

    case 1007: { // PLAYER_LEAVE_NOTIFY — 플레이어 접속 해제
        player_leave_notify notify{};
        if (player_leave_notify::parse(p, end, notify)) {
            std::cout << "[" << player_name_ << "] <- LEAVE player=" << notify.channel_id << "\n";
            std::lock_guard<std::mutex> lk(nearby_mutex_);
            nearby_players_.erase(notify.channel_id);
        }
        break;
    }

    case 1006: { // ENTITY_DEATH_NOTIFY
        using namespace lemondory::shared;
        entity_death_notify notify{};
        if (entity_death_notify::parse(p, end, notify)) {
            std::cout << "[" << player_name_ << "] <- DEATH entity=" << notify.entity_id
                      << " killer=" << notify.killer_id << "\n";
            std::lock_guard<std::mutex> lk(nearby_mutex_);
            nearby_players_.erase(notify.entity_id);
        }
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
