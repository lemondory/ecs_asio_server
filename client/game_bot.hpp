#pragma once
#include "common/network/frame_codec.hpp"
#include "common/protocol/protocol_base.hpp"
#include <asio.hpp>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lemondory::client {

// GameBot — 봇 클라이언트 (Phase 3 호환 설계)
//
// Phase 1 (현재): authenticate()="" → game server 직접 접속
// Phase 3 (예정): authenticate()=JWT → front server 접속
// 바뀌는 것은 authenticate(), game_host(), game_port() 세 함수뿐.
class GameBot {
public:
    explicit GameBot(std::string player_name,
                     std::string host = "127.0.0.1",
                     int port = 12345);
    ~GameBot();

    bool connect();
    void run(int move_count = 10);
    void disconnect();

private:
    // Phase 1: returns "". Phase 3: HTTP POST to auth server → JWT.
    std::string authenticate();

    void send_frame(std::uint16_t cmd, const char* data, std::size_t size);
    void send_login();
    void send_map_enter(int map_id, float x = 0.f, float y = 0.f, float z = 0.f);
    void send_move(float x, float y, float z);
    void send_chat(const std::string& msg);
    void send_ping();

    void recv_loop();
    void on_frame(std::uint16_t cmd, std::string_view payload);

    std::string player_name_;
    std::string session_token_;
    std::string host_;
    int port_;

    asio::io_context io_;
    asio::ip::tcp::socket socket_;
    std::atomic<bool> running_{false};
    std::thread recv_thread_;
    std::mutex send_mutex_;

    std::vector<char> acc_;
    std::size_t acc_offset_{0};

    // 상태 플래그 (recv 스레드에서 세트, run()에서 대기)
    std::atomic<bool> in_map_{false};
    int spawn_map_id_{1};
};

} // namespace lemondory::client
