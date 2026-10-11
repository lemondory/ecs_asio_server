#include "server/game/game_server.hpp"
#include "common/log.hpp"
#include "common/protocol/protocol_player.hpp"
#include "common/protocol/protocol_combat.hpp"
#include "common/protocol/protocol_map.hpp"
#include "server/data/data_manager.hpp"
#include <cstring>

namespace lemondory::game {
using namespace lemondory::network;

// ── 핸들러 등록 ──────────────────────────────────────────────────────────────

void game_server::bind_player_handlers() {
    // TODO(cmd-enum): cmd 번호(1001~1008, 2001~)가 매직 넘버로 흩어져 있음 — 공용 enum으로 통합.
    //                 MapThread 내부 cmd(0x1001~)와 번호 체계도 다르므로 함께 정리.
    dispatcher_.bind(1001, [this](int ch, const char* d, std::size_t s) { handle_login(ch, d, s); });
    dispatcher_.bind(1002, [this](int ch, const char* d, std::size_t s) { handle_move(ch, d, s); });
    dispatcher_.bind(1003, [this](int ch, const char* d, std::size_t s) { handle_chat(ch, d, s); });
    dispatcher_.bind(1004, [this](int ch, const char* d, std::size_t s) { handle_attack(ch, d, s); });
    dispatcher_.bind(1005, [this](int ch, const char* d, std::size_t s) { handle_map_enter(ch, d, s); });
}

// ── 1001 LOGIN ───────────────────────────────────────────────────────────────

void game_server::handle_login(int channel_id, const char* data, std::size_t size) {
    using namespace lemondory::shared;

    login_req req;
    if (!login_req::parse(data, data + size, req)) {
        LOGW("handle_login: parse failed (channel={})", channel_id);
        return;
    }
    LOGI("Player login: {} (channel={})", req.player_name, channel_id);

#ifdef LEMONDORY_HAVE_REDIS
    // 세션 토큰 검증 — Redis에 등록된 토큰이 아니면 거부
    if (redis_client_ && !req.session_token.empty()) {
        auto pid = redis_client_->get_player_id_by_token(req.session_token);
        if (!pid) {
            LOGW("handle_login: invalid token (channel={} name={})", channel_id, req.player_name);
            socket_channel_base* ch = nullptr;
            { std::lock_guard<std::mutex> lk(mtx_);
              auto it = channels_.find(channel_id);
              if (it != channels_.end()) ch = it->second; }
            if (ch) {
                using namespace lemondory::shared;
                login_res err{false, "invalid_token", 0, 0};
                auto payload = err.serialize();
                send_frame(ch, 1001, payload.data(), payload.size());
            }
            return;
        }
        LOGD("Token valid: channel={} player_id={}", channel_id, *pid);
    }
#endif

#ifdef LEMONDORY_HAVE_MYSQL
    if (player_dao_) {
        std::optional<db_strand_t> strand_opt;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto it = db_strands_.find(channel_id);
            if (it == db_strands_.end()) return;
            strand_opt = it->second;
        }

        auto* dao       = player_dao_.get();
        auto  io_exec   = io_exec_;
        std::string player_name = req.player_name;
        bool use_ecs    = config_.use_ecs;

        auto* redis = redis_client_.get();
        int player_ttl_sec = config_.redis.player_ttl_sec;
        asio::post(*strand_opt,
            [this, dao, redis, io_exec, channel_id, player_name, use_ecs, player_ttl_sec]() {
                std::optional<lemondory::db::PlayerRecord> rec;
                try {
#ifdef LEMONDORY_HAVE_REDIS
                    // 캐시 히트 시 DB 조회 생략 (future: deserialize cached JSON)
                    if (redis) {
                        auto cached = redis->get_player_cache(0); // placeholder: id 미확정
                        (void)cached; // TODO(redis-cache): id 확정 후 캐시 우선 조회로 교체
                    }
#endif
                    rec = dao->find_or_create(player_name);
#ifdef LEMONDORY_HAVE_REDIS
                    // DB 조회 성공 시 Redis 캐싱
                    if (redis && rec) {
                        std::string json = "{\"id\":" + std::to_string(rec->id) +
                                           ",\"name\":\"" + player_name + "\"" +
                                           ",\"map_id\":" + std::to_string(rec->map_id) +
                                           ",\"hp\":" + std::to_string(rec->hp) + "}";
                        redis->set_player_cache(rec->id, json, player_ttl_sec);
                        LOGD("Player cache set: id={} ttl={}s", rec->id, player_ttl_sec);
                    }
#endif
                } catch (const std::exception& e) {
                    LOGE("DB thread exception (login): channel={} what={}", channel_id, e.what());
                } catch (...) {
                    LOGE("DB thread unknown exception (login): channel={}", channel_id);
                }
                asio::post(io_exec,
                    [this, rec, channel_id, player_name, use_ecs]() {
                        // orphaned-entity 방지
                        {
                            std::lock_guard<std::mutex> lk(mtx_);
                            if (!channels_.count(channel_id)) return;
                        }
                        if (!rec) {
                            LOGE("handle_login: DB 실패로 로그인 거부 (channel={} name={})",
                                 channel_id, player_name);
                            socket_channel_base* ch = nullptr;
                            { std::lock_guard<std::mutex> lk(mtx_);
                              auto it = channels_.find(channel_id);
                              if (it != channels_.end()) ch = it->second; }
                            if (ch) {
                                using namespace lemondory::shared;
                                login_res err{false, "db_error", 0, 0};
                                auto payload = err.serialize();
                                send_frame(ch, 1001, payload.data(), payload.size());
                            }
                            return;
                        }

                        std::int64_t db_id    = rec->id;
                        int  spawn_map_id     = rec->map_id;
                        float spawn_x = rec->pos_x, spawn_y = rec->pos_y, spawn_z = rec->pos_z;
                        int  spawn_hp = rec->hp,    spawn_max_hp = rec->max_hp;
                        // 사망 상태(hp=0)로 저장된 경우 재접속 시 최대 HP로 부활
                        // TODO(respawn): 부활 시스템(대기 시간·부활 위치) 설계 후 교체
                        if (spawn_hp <= 0) spawn_hp = spawn_max_hp;
                        LOGI("Player {}: db_id={} map={} pos=({:.1f},{:.1f},{:.1f})",
                             player_name, db_id, spawn_map_id, spawn_x, spawn_y, spawn_z);

                        Entity player = 0;
                        if (use_ecs) {
                            player = game_service().create_entity();
                            game_service().emplace_component<Position>(player, spawn_x, spawn_y, spawn_z);
                            game_service().emplace_component<Velocity>(player, 0.f, 0.f, 0.f);
                            game_service().emplace_component<Player>(player,
                                player_name,
                                static_cast<std::uint32_t>(db_id > 0 ? db_id : player),
                                channel_id,
                                static_cast<std::uint32_t>(spawn_map_id));
                            {
                                // DB MP 미지원 시 PlayerLevel.csv 레벨 1 MP 사용
                                // TODO(level-stats): rec->level 기준으로 MP·ATK·DEF 조회 (현재 레벨 1 고정)
                                const auto* lv =
                                    lemondory::data::DataManager::instance().get_player_level(1);
                                const uint32_t init_mp =
                                    lv ? static_cast<uint32_t>(lv->mp) : 50u;
                                game_service().emplace_component<Health>(player,
                                    static_cast<std::uint32_t>(spawn_max_hp), init_mp);
                            }
                            if (auto* h = game_service().get_component<Health>(player))
                                h->hp = static_cast<std::uint32_t>(spawn_hp);
                            {
                                std::lock_guard<std::mutex> lk(mtx_);
                                channel_to_entity_[channel_id] = player;
                                if (db_id > 0) channel_to_db_id_[channel_id] = db_id;
                            }
                            LOGD("Player ECS entity={} db_id={}", static_cast<int>(player), db_id);
                        }

                        socket_channel_base* ch = nullptr;
                        { std::lock_guard<std::mutex> lk(mtx_);
                          auto it = channels_.find(channel_id);
                          if (it != channels_.end()) ch = it->second; }
                        if (!ch) return;

                        using namespace lemondory::shared;
                        // TODO(id-unify): 클라이언트에 db_id를 주지만 인게임 패킷(이동·전투·사망)은
                        //                 channel_id를 사용 → 클라이언트가 자기 id를 식별할 수 없음.
                        std::uint32_t resp_id = db_id > 0
                            ? static_cast<std::uint32_t>(db_id)
                            : static_cast<std::uint32_t>(player);
                        login_res res{true, "ok", resp_id,
                                      static_cast<std::uint32_t>(spawn_map_id)};
                        auto payload = res.serialize();
                        send_frame(ch, 1001, payload.data(), payload.size());
                    });
            });
        return;
    }
#endif

    // DB 없는 경우: 기본값으로 즉시 응답
    Entity player = 0;
    if (config_.use_ecs) {
        player = game_service().create_entity();
        game_service().emplace_component<Position>(player, 0.f, 0.f, 0.f);
        game_service().emplace_component<Velocity>(player, 0.f, 0.f, 0.f);
        game_service().emplace_component<Player>(player,
            req.player_name, static_cast<std::uint32_t>(player), channel_id, 1u);
        {
            // TODO(level-stats): DB 없는 경로 — 레벨 1·맵 1 고정 스폰
            const auto* lv =
                lemondory::data::DataManager::instance().get_player_level(1);
            const uint32_t init_hp = lv ? static_cast<uint32_t>(lv->hp) : 100u;
            const uint32_t init_mp = lv ? static_cast<uint32_t>(lv->mp) : 50u;
            game_service().emplace_component<Health>(player, init_hp, init_mp);
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            channel_to_entity_[channel_id] = player;
        }
        LOGD("Player ECS entity={} (no DB)", static_cast<int>(player));
    }

    socket_channel_base* ch = nullptr;
    { std::lock_guard<std::mutex> lk(mtx_);
      auto it = channels_.find(channel_id);
      if (it != channels_.end()) ch = it->second; }
    if (!ch) return;

    login_res res{true, "ok", static_cast<std::uint32_t>(player), 1u};
    auto payload = res.serialize();
    send_frame(ch, 1001, payload.data(), payload.size());
}

// ── 1002 MOVE ────────────────────────────────────────────────────────────────

void game_server::handle_move(int channel_id, const char* data, std::size_t size) {
    using namespace lemondory::shared;

    player_move_req req;
    if (!player_move_req::parse(data, data + size, req)) {
        LOGW("Invalid move data from channel {}", channel_id);
        return;
    }

    if (main_thread_manager_) {
        route_packet_to_map(channel_id, 0x1001, data, size);
        return;
    }

    LOGD("Player move: ({}, {}, {}) from channel={}", req.x, req.y, req.z, channel_id);

    if (config_.use_ecs) {
        Entity player_entity = 0;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto it = channel_to_entity_.find(channel_id);
            if (it != channel_to_entity_.end()) player_entity = it->second;
        }
        if (player_entity) {
            auto* position = game_service().get_component<Position>(player_entity);
            if (position) {
                position->x = req.x;
                position->y = req.y;
                position->z = req.z;
            }
        }
    }

    std::vector<char> bcast;
    write_le32(bcast, static_cast<std::uint32_t>(channel_id));
    write_float_le(bcast, req.x);
    write_float_le(bcast, req.y);
    write_float_le(bcast, req.z);
    broadcast_except(channel_id, 1002, bcast.data(), bcast.size());

    const char ok[] = "ok";
    send_test_frame(channel_id, 1002, ok, sizeof(ok) - 1);
}

// ── 1003 CHAT ────────────────────────────────────────────────────────────────

void game_server::handle_chat(int channel_id, const char* data, std::size_t size) {
    std::string message(data, size);
    LOGI("Chat from channel={}: {}", channel_id, message);

    if (main_thread_manager_) {
        main_thread_manager_->handle_global_chat(channel_id, message);
        return;
    }

    std::vector<char> chat_broadcast;
    lemondory::shared::write_le32(chat_broadcast, static_cast<std::uint32_t>(channel_id));
    chat_broadcast.insert(chat_broadcast.end(), message.begin(), message.end());
    broadcast(1003, chat_broadcast.data(), chat_broadcast.size());

    const char* response = "CHAT_SUCCESS";
    send_test_frame(channel_id, 1003, response, strlen(response));
}

// ── 1004 ATTACK ──────────────────────────────────────────────────────────────

void game_server::handle_attack(int channel_id, const char* data, std::size_t size) {
    using namespace lemondory::shared;

    attack_req req;
    if (!attack_req::parse(data, data + size, req)) {
        LOGW("Invalid attack_req from channel={}", channel_id);
        return;
    }

    // TODO(id-unify): target_id를 channel_id로 해석 — 몬스터 타겟 불가.
    //                 entity_id 기반으로 바꾸고 플레이어/몬스터 구분 필요.
    int target_channel = static_cast<int>(req.target_id);
    LOGI("Attack channel={} -> target_channel={} skill={}", channel_id, target_channel, req.skill_id);

    Entity attacker_entity = 0, target_entity = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (auto it = channel_to_entity_.find(channel_id); it != channel_to_entity_.end())
            attacker_entity = it->second;
        if (auto it = channel_to_entity_.find(target_channel); it != channel_to_entity_.end())
            target_entity = it->second;
    }

    // entity 조회 실패: 상대방이 이미 접속 해제한 극소수 타이밍 케이스
    if (!attacker_entity || !target_entity) {
        LOGD("Attack: entity not found (attacker={} target={}) — disconnect race",
             attacker_entity, target_entity);
        return;
    }

    auto* combat = game_service().get_system<CombatSystem>();
    if (!combat) return;

    // 상태 체크: 공격자가 사망 상태면 거부
    if (combat->get_hp(attacker_entity) == 0) {
        attack_res res{false, "you are dead", 0, 0, 0};
        auto payload = res.serialize();
        send_test_frame(channel_id, 1004, payload.data(), payload.size());
        return;
    }

    // 상태 체크: 타겟이 이미 사망 상태면 거부
    std::uint32_t target_current_hp = combat->get_hp(target_entity);
    if (target_current_hp == 0) {
        attack_res res{false, "target is dead", 0, 0, 0};
        auto payload = res.serialize();
        send_test_frame(channel_id, 1004, payload.data(), payload.size());
        return;
    }

    // TODO(combat-damage): 스탯 기반 데미지 계산으로 교체 필요.
    // 공격자 ATK, 방어자 DEF, 스킬 배율(SkillData) 설계 확정 후 CombatSystem::calculate_damage()로 위임.
    std::uint32_t damage = 10 + req.skill_id * 5;
    combat->take_damage(target_entity, damage);

    std::uint32_t remaining_hp = combat->get_hp(target_entity);
    bool target_dead = (remaining_hp == 0);

    attack_res res{true, target_dead ? "killed" : "hit", 1, damage, remaining_hp};
    auto res_payload = res.serialize();
    send_test_frame(channel_id, 1004, res_payload.data(), res_payload.size());

    damage_notify notify{
        static_cast<std::uint32_t>(channel_id),
        req.target_id, damage, remaining_hp, 1, req.timestamp
    };
    auto notify_payload = notify.serialize();
    aoi_broadcast(channel_id, 1008, notify_payload.data(), notify_payload.size(), channel_id);

    if (target_dead) {
        LOGI("Entity {} killed by channel={}", static_cast<int>(target_entity), channel_id);
        // entity와 channel_to_entity_를 유지 — hp=0으로 사망 상태 표현, 부활 시 재사용
        entity_death_notify death{
            req.target_id,
            static_cast<std::uint32_t>(channel_id),
            req.timestamp
        };
        auto death_payload = death.serialize();
        aoi_broadcast(target_channel, 1006, death_payload.data(), death_payload.size());
    }
}

// ── 1005 MAP_ENTER ───────────────────────────────────────────────────────────

void game_server::handle_map_enter(int channel_id, const char* data, std::size_t size) {
    using namespace lemondory::shared;

    map_enter_req req;
    if (!map_enter_req::parse(data, data + size, req)) {
        LOGW("Invalid map_enter_req from channel={}", channel_id);
        return;
    }

    int target_map_id = static_cast<int>(req.map_id);
    LOGI("Map enter request: channel={} -> map={}", channel_id, target_map_id);

    const auto* map_cfg = lemondory::data::DataManager::instance().get_map(target_map_id);

    if (!map_cfg) {
        LOGW("Map {} not found", target_map_id);
        map_enter_res res{false, "map not found", 0, "", 0, 0, 0, 0};
        auto payload = res.serialize();
        send_test_frame(channel_id, 1005, payload.data(), payload.size());
        return;
    }

    if (main_thread_manager_) {
        int current_map = get_player_map(channel_id);
        if (current_map != target_map_id) {
            update_player_map(channel_id, target_map_id);

            Entity entity = 0;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                if (auto it = channel_to_entity_.find(channel_id); it != channel_to_entity_.end()) {
                    entity = it->second;
                    if (auto* player = game_service().get_component<Player>(entity))
                        player->map_id = static_cast<std::uint32_t>(target_map_id);
                }
            }
            main_thread_manager_->handle_map_transfer(channel_id, current_map, target_map_id, entity);
        }
    }

    map_enter_res res{true, "ok", req.map_id, map_cfg->name, req.x, req.y, req.z, 0};
    auto payload = res.serialize();
    send_test_frame(channel_id, 1005, payload.data(), payload.size());
    LOGI("channel={} entered map={} ({})", channel_id, target_map_id, map_cfg->name);
}

} // namespace lemondory::game
