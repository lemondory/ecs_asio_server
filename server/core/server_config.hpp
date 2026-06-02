#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>

namespace lemondory::core {

// config/server_config.json 이 없을 때 사용되는 기본값
// password 는 빈 문자열 — 실제 값은 반드시 설정 파일 또는 환경변수로 주입
struct DbConfig {
    bool        enabled   = false;
    std::string host      = "127.0.0.1";
    uint16_t    port      = 3306;
    std::string name      = "lemondory_game";
    std::string user      = "game_user";
    std::string password  = "";  // 운영 환경: 환경변수/시크릿으로 관리
    int         pool_size = 4;
};

struct RedisConfig {
    bool        enabled         = false;
    std::string host            = "127.0.0.1";
    uint16_t    port            = 6379;
    std::string password        = "";
    int         db_index        = 0;
    int         token_ttl_sec   = 7200;   // 세션 토큰 TTL (비정상 종료 안전망)
    int         player_ttl_sec  = 1800;   // 플레이어 데이터 캐시 TTL
};

struct ServerConfig {
    // ── 네트워크 ─────────────────────────────────────────────────
    std::string host            = "0.0.0.0";
    uint16_t    port            = 12345;
    int         backlog         = 128;
    int         max_connections = 1000;
    bool        tcp_no_delay    = true;

    // ── 로깅 ─────────────────────────────────────────────────────
    std::string log_level       = "debug";   // 운영 환경: "info" 또는 "warn" 권장
    bool        log_file        = false;
    std::string log_file_path   = "logs/server.log";

    // ── 스레딩 ───────────────────────────────────────────────────
    int         io_threads          = 1;     // asio io_context 스레드 수 (현재 미사용, 확장용)
    bool        map_threads_enabled = true;

    // ── 게임 ─────────────────────────────────────────────────────
    int  tick_rate        = 60;
    bool use_ecs          = true;
    int  memory_pool_size = 1000;

    // ── DB / Redis ───────────────────────────────────────────────
    // key: "game" | "guild" | "common" — role별 독립 연결 설정.
    // Phase 1: 세 role 모두 동일 DB 인스턴스를 가리켜도 됨.
    // Phase 3: host만 바꾸면 물리 분리.
    std::unordered_map<std::string, DbConfig> databases = {
        {"game",   {}},
        {"guild",  {}},
        {"common", {}},
    };
    RedisConfig redis;

    // config/server_config.json 파일이 있으면 덮어쓰고,
    // 없거나 파싱 실패하면 위 기본값 그대로 사용
    static ServerConfig load(const std::string& path = "config/server_config.json");
};

} // namespace lemondory::core
