#include "server_config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include "common/log.hpp"

namespace lemondory::core {

using json = nlohmann::json;

ServerConfig ServerConfig::load(const std::string& path) {
    ServerConfig cfg;

    std::ifstream f(path);
    if (!f.is_open()) {
        LOGI("Config file not found ({}), using defaults.", path);
        return cfg;
    }

    json j;
    try {
        j = json::parse(f, nullptr, true, true);  // ignore_comments=true: // 주석 허용
    } catch (const std::exception& e) {
        LOGW("Config parse error ({}): {}. Using defaults.", path, e.what());
        return cfg;
    }

    // ── 네트워크 ─────────────────────────────────────────────────
    if (auto s = j.find("server"); s != j.end()) {
        if (s->contains("host"))            cfg.host            = (*s)["host"].get<std::string>();
        if (s->contains("port"))            cfg.port            = (*s)["port"].get<uint16_t>();
        if (s->contains("backlog"))         cfg.backlog         = (*s)["backlog"].get<int>();
        if (s->contains("max_connections")) cfg.max_connections = (*s)["max_connections"].get<int>();
        if (s->contains("tcp_no_delay"))    cfg.tcp_no_delay    = (*s)["tcp_no_delay"].get<bool>();
        if (s->contains("tick_rate"))       cfg.tick_rate       = (*s)["tick_rate"].get<int>();
    }

    // ── 로깅 ─────────────────────────────────────────────────────
    if (auto l = j.find("logging"); l != j.end()) {
        if (l->contains("level"))          cfg.log_level     = (*l)["level"].get<std::string>();
        if (l->contains("file"))           cfg.log_file      = (*l)["file"].get<bool>();
        if (l->contains("file_path"))      cfg.log_file_path = (*l)["file_path"].get<std::string>();
    }

    // ── 스레딩 ───────────────────────────────────────────────────
    if (auto t = j.find("threading"); t != j.end()) {
        if (t->contains("io_threads"))          cfg.io_threads          = (*t)["io_threads"].get<int>();
        if (t->contains("map_threads_enabled")) cfg.map_threads_enabled = (*t)["map_threads_enabled"].get<bool>();
    }

    // ── 게임 ─────────────────────────────────────────────────────
    if (auto g = j.find("game"); g != j.end()) {
        if (g->contains("use_ecs"))          cfg.use_ecs          = (*g)["use_ecs"].get<bool>();
        if (g->contains("memory_pool_size")) cfg.memory_pool_size = (*g)["memory_pool_size"].get<int>();
    }

    // ── DB (role별 개별 설정) ─────────────────────────────────────
    if (j.contains("databases") && j["databases"].is_object()) {
        for (auto& [key, val] : j["databases"].items()) {
            DbConfig dc;
            if (val.contains("enabled"))   dc.enabled   = val["enabled"].get<bool>();
            if (val.contains("host"))      dc.host      = val["host"].get<std::string>();
            if (val.contains("port"))      dc.port      = val["port"].get<uint16_t>();
            if (val.contains("name"))      dc.name      = val["name"].get<std::string>();
            if (val.contains("user"))      dc.user      = val["user"].get<std::string>();
            if (val.contains("password"))  dc.password  = val["password"].get<std::string>();
            if (val.contains("pool_size")) dc.pool_size = val["pool_size"].get<int>();
            cfg.databases[key] = dc;
        }
    }

    // ── Redis ────────────────────────────────────────────────────
    if (auto r = j.find("redis"); r != j.end()) {
        if (r->contains("enabled"))        cfg.redis.enabled        = (*r)["enabled"].get<bool>();
        if (r->contains("host"))           cfg.redis.host           = (*r)["host"].get<std::string>();
        if (r->contains("port"))           cfg.redis.port           = (*r)["port"].get<uint16_t>();
        if (r->contains("password"))       cfg.redis.password       = (*r)["password"].get<std::string>();
        if (r->contains("db_index"))       cfg.redis.db_index       = (*r)["db_index"].get<int>();
        if (r->contains("token_ttl_sec"))  cfg.redis.token_ttl_sec  = (*r)["token_ttl_sec"].get<int>();
        if (r->contains("player_ttl_sec")) cfg.redis.player_ttl_sec = (*r)["player_ttl_sec"].get<int>();
    }

    LOGI("Config loaded from {}.", path);
    return cfg;
}

} // namespace lemondory::core
