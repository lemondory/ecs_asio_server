#include "game_server.hpp"
#include "common/log.hpp"
#include "common/protocol/protocol_player.hpp"
#include "server/data/data_manager.hpp"
#include <cstring>

namespace lemondory::game {
using namespace lemondory::network;

bool game_server::init(asio::io_context& io,
                       const socket_option& opt,
                       const std::string& ip, int port)
{
    // ECS 시스템 초기화
    if (config_.use_ecs) {
        if (!game_service().initialize()) {
            LOGE("ECS game service init failed");
            return false;
        }
        LOGI("ECS system initialized");

        // 스레딩 시스템 초기화
        initialize_threading_system();
    }

#ifdef LEMONDORY_HAVE_MYSQL
    {
        int n = 4;
        auto it = config_.databases.find("game");
        if (it != config_.databases.end() && it->second.enabled)
            n = std::max(1, it->second.pool_size);
        db_pool_.emplace(static_cast<std::size_t>(n));
        LOGI("DB thread pool: {} thread(s)", n);
    }
    failure_sink_ = std::make_unique<lemondory::db::FailureSink>();
    db_manager_ = std::make_unique<lemondory::db::DbManager>();
    if (db_manager_->init(config_.databases)) {
        if (auto* gp = db_manager_->pool(lemondory::db::DbRole::Game))
            player_dao_ = std::make_unique<lemondory::db::PlayerDao>(*gp);
    } else {
        LOGW("No DB connections opened — running without persistent storage");
        db_manager_.reset();
    }
#endif

#ifdef LEMONDORY_HAVE_REDIS
    if (config_.redis.enabled) {
        lemondory::db::RedisClient::Config rcfg;
        rcfg.host     = config_.redis.host;
        rcfg.port     = config_.redis.port;
        rcfg.password = config_.redis.password;
        rcfg.db_index = config_.redis.db_index;
        redis_client_ = std::make_unique<lemondory::db::RedisClient>(rcfg);
        if (!redis_client_->connect()) {
            LOGW("Redis connect failed — running without Redis cache");
            redis_client_.reset();
        }
    }
#endif

    io_exec_ = io.get_executor();
    flush_timer_.emplace(io);

    server_ = std::make_shared<asio_server>(io);
    if (!server_->init(this, opt, 0, 0)) {
        LOGE("Server init failed"); return false;
    }
    LOGI("Server listening on " + ip + ":" + std::to_string(port));

    server_->set_on_frame(
    [this](int channel_id, std::uint16_t cmd, const char* data, std::size_t size)
    {
        dispatcher_.dispatch(cmd, channel_id, data, size);
    });

    // PING(1) → PONG(2)
    dispatcher_.bind(1, [this](int channel_id, const char* /*data*/, std::size_t /*size*/) {
        socket_channel_base* ch = nullptr;
        { std::lock_guard<std::mutex> lk(mtx_);
          auto it = channels_.find(channel_id);
          if (it != channels_.end()) ch = it->second;
        }
        const char pong[] = "PONG";
        if (ch) send_frame(ch, 2, pong, sizeof(pong) - 1);
    });

    // ECHO(100)
    dispatcher_.bind(100, [this](int channel_id, const char* data, std::size_t size) {
        socket_channel_base* ch = nullptr;
        { std::lock_guard<std::mutex> lk(mtx_);
          auto it = channels_.find(channel_id);
          if (it != channels_.end()) ch = it->second;
        }
        if (ch) send_frame(ch, 100, data, size);
    });

    // 게임 핸들러 등록 (handlers/*.cpp 참조)
    bind_player_handlers();
    bind_guild_handlers();

    if (!server_->listen(ip, port)) {
        LOGE("Server listen failed on " + ip + ":" + std::to_string(port));
        return false;
    }

#ifdef LEMONDORY_HAVE_MYSQL
    if (player_dao_) schedule_flush();
#endif

    return true;
}

void game_server::stop() {
    if (server_) { server_->stop(); server_.reset(); }

    // 타이머 취소 → 더 이상 flush 예약 안 함
    if (flush_timer_) { flush_timer_->cancel(); flush_timer_.reset(); }

    shutdown_threading_system();

#ifdef LEMONDORY_HAVE_MYSQL
    if (db_pool_) db_pool_->join();  // pending DB 작업 완료 후 종료
    player_dao_.reset();
    if (db_manager_) { db_manager_->close(); db_manager_.reset(); }
#endif

#ifdef LEMONDORY_HAVE_REDIS
    if (redis_client_) redis_client_->disconnect();
#endif

    std::lock_guard<std::mutex> lk(mtx_);
    channels_.clear();
}

void game_server::on_channel_init(socket_channel_base* channel, bool /*use_plugin*/) {
    LOGD("channel init id={}", channel->get_channel_id());
}

void game_server::on_channel_destroy(socket_channel_base* channel) {
    LOGD("channel destroy id={}", channel->get_channel_id());
}

void game_server::on_accept(int channel_id, socket_channel_base* channel) {
    std::lock_guard<std::mutex> lk(mtx_);
    channels_[channel_id] = channel;
#ifdef LEMONDORY_HAVE_MYSQL
    if (db_pool_) db_strands_.emplace(channel_id, db_pool_->get_executor());
#endif

    LOGI("accept: channel={}", channel_id);
}

void game_server::on_close(int channel_id, const void* /*error*/, close_function reason) {
    Entity entity = 0;
    std::int64_t db_id = 0;
#ifdef LEMONDORY_HAVE_MYSQL
    std::optional<db_strand_t> strand_opt;
#endif
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (auto it = channel_to_entity_.find(channel_id); it != channel_to_entity_.end())
            entity = it->second;
        channels_.erase(channel_id);
        channel_to_entity_.erase(channel_id);
#ifdef LEMONDORY_HAVE_MYSQL
        if (auto it = channel_to_db_id_.find(channel_id); it != channel_to_db_id_.end()) {
            db_id = it->second;
            channel_to_db_id_.erase(it);
        }
        if (auto it = db_strands_.find(channel_id); it != db_strands_.end()) {
            strand_opt = it->second;
            db_strands_.erase(it);
        }
#endif
    }

#ifdef LEMONDORY_HAVE_MYSQL
    if (strand_opt && player_dao_ && db_id > 0 && entity != 0) {
        float x = 0.f, y = 0.f, z = 0.f;
        int   map_id = 1, level = 1, exp = 0, hp = 100;
        std::string player_name;
        if (auto* pos    = game_service().get_component<Position>(entity))
            { x = pos->x; y = pos->y; z = pos->z; }
        if (auto* player = game_service().get_component<Player>(entity))
            { map_id = static_cast<int>(player->map_id);
              player_name = player->player_name; }
        if (auto* health = game_service().get_component<Health>(entity))
            { hp = static_cast<int>(health->hp); }

        auto* dao  = player_dao_.get();
        auto* sink = failure_sink_.get();
        asio::post(strand_opt.value(), [dao, sink, db_id, x, y, z,
                                        map_id, level, exp, hp, player_name]() {
            try {
                bool pos_ok   = dao->save_position(db_id, x, y, z, map_id);
                bool stats_ok = dao->save_stats(db_id, level, exp, hp);
                if (pos_ok && stats_ok) {
                    LOGI("Player id={} saved (map={} pos={:.1f},{:.1f},{:.1f})", db_id, map_id, x, y, z);
                } else if (sink) {
                    lemondory::db::PlayerSaveSnapshot snap;
                    snap.player_id   = db_id;
                    snap.player_name = player_name;
                    snap.operation   = (!pos_ok ? "save_position" : "save_stats");
                    snap.pos_x = x; snap.pos_y = y; snap.pos_z = z;
                    snap.map_id      = map_id;
                    snap.level = level; snap.exp = exp; snap.hp = hp;
                    snap.attempts    = 3;
                    snap.error_msg   = "db_write_failed";
                    sink->record(snap);
                }
            } catch (const std::exception& e) {
                LOGE("DB thread exception (on_close): player_id={} what={}", db_id, e.what());
                if (sink) {
                    lemondory::db::PlayerSaveSnapshot snap;
                    snap.player_id = db_id; snap.player_name = player_name;
                    snap.operation = "on_close"; snap.error_msg = e.what();
                    snap.pos_x = x; snap.pos_y = y; snap.pos_z = z;
                    snap.map_id = map_id; snap.level = level; snap.exp = exp; snap.hp = hp;
                    snap.attempts = 1;
                    sink->record(snap);
                }
            } catch (...) {
                LOGE("DB thread unknown exception (on_close): player_id={}", db_id);
            }
        });
    }
#endif

#ifdef LEMONDORY_HAVE_REDIS
    if (redis_client_ && db_id > 0)
        redis_client_->del_player_cache(db_id);
#endif

    if (main_thread_manager_) {
        // remove_player 전에 셀 정보가 유효할 때 퇴장 알림 전송
        if (entity != 0) {
            lemondory::shared::player_leave_notify leave{
                static_cast<std::uint32_t>(channel_id)
            };
            auto leave_payload = leave.serialize();
            aoi_broadcast(channel_id, 1007, leave_payload.data(), leave_payload.size(), channel_id);
        }
        main_thread_manager_->remove_player(channel_id);
        std::lock_guard<std::mutex> map_lock(player_map_mutex_);
        player_map_mapping_.erase(channel_id);
    }

    if (entity != 0 && config_.use_ecs)
        game_service().destroy_entity(entity);

    LOGI("close: channel={} reason={}", channel_id, static_cast<int>(reason));
}

void game_server::on_tick() {
    if (config_.use_ecs) {
        // TODO(tick-dt): dt 0.016 고정 — config tick_rate 또는 실측 경과 시간 사용
        game_service().update(0.016f);
    }
}

void game_server::send_frame(socket_channel_base* ch,
                             std::uint16_t msg_id,
                             const char* data, std::size_t size) {
    auto pkt = lemondory::network::frame_codec::pack(
        msg_id, 0, 1, 0, std::string_view{data, size}
    );
    ch->send(pkt.data(), static_cast<int>(pkt.size()));
}

void game_server::send_test_frame(int channel_id, std::uint16_t msg_id, const char* data, std::size_t size) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = channels_.find(channel_id);
    if (it != channels_.end()) {
        send_frame(it->second, msg_id, data, size);
    }
}

void game_server::broadcast(std::uint16_t cmd, const char* data, std::size_t size) {
    auto pkt = lemondory::network::frame_codec::pack(cmd, 0, 1, 0, std::string_view{data, size});
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& [id, ch] : channels_) {
        ch->send(pkt.data(), static_cast<int>(pkt.size()));
    }
}

void game_server::broadcast_except(int exclude_channel_id, std::uint16_t cmd, const char* data, std::size_t size) {
    auto pkt = lemondory::network::frame_codec::pack(cmd, 0, 1, 0, std::string_view{data, size});
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& [id, ch] : channels_) {
        if (id != exclude_channel_id) {
            ch->send(pkt.data(), static_cast<int>(pkt.size()));
        }
    }
}

// ── 스레딩 시스템 ────────────────────────────────────────────────────────────

void game_server::initialize_threading_system() {
    // 플레이어 컴포넌트: max_connections 기준으로 사전 예약 → 런타임 reallocation 없음
    const std::size_t player_cap = static_cast<std::size_t>(config_.max_connections) + 16;
    auto& gs = game_service();
    gs.reserve_component<Position>(player_cap);
    gs.reserve_component<Velocity>(player_cap);
    gs.reserve_component<Health>(player_cap);
    gs.reserve_component<Player>(player_cap);
    gs.reserve_component<GameObject>(player_cap);
    LOGI("ECS component stores reserved for {} players", player_cap);

    // 게임 데이터 로드 (data/Map.csv, Monster.csv, MonsterSpawn.csv)
    lemondory::data::DataManager::instance().load("data");

    const int tick_ms = (config_.tick_rate > 0) ? (1000 / config_.tick_rate) : 16;

    main_thread_manager_ = std::make_unique<MainThreadManager>();
    main_thread_manager_->start(tick_ms);

    const auto& maps = lemondory::data::DataManager::instance().all_maps();
    for (const auto& m : maps) {
        main_thread_manager_->create_map_thread(
            m.id, m.name, m.width, m.height, m.max_players,
            m.max_monsters, m.spawn_interval, m.monster_spawn, tick_ms);
    }

    // AOI 브로드캐스트 콜백 등록 — MapThread → 네트워크 채널로 패킷 전송
    main_thread_manager_->set_sender(
        [this](int ch_id, std::uint16_t cmd, const char* data, std::size_t sz) {
            std::lock_guard<std::mutex> lk(mtx_);
            auto it = channels_.find(ch_id);
            if (it != channels_.end())
                send_frame(it->second, cmd, data, sz);
        }
    );

    LOGI("Threading system initialized with {} map(s).", maps.size());
}

void game_server::shutdown_threading_system() {
    if (main_thread_manager_) {
        LOGI("Shutting down threading system...");
        main_thread_manager_->stop();
        main_thread_manager_.reset();
        LOGI("Threading system shutdown complete.");
    }
}

int game_server::get_player_map(int channel_id) {
    std::lock_guard<std::mutex> lock(player_map_mutex_);
    auto it = player_map_mapping_.find(channel_id);
    return it != player_map_mapping_.end() ? it->second : -1;
}

void game_server::update_player_map(int channel_id, int map_id) {
    std::lock_guard<std::mutex> lock(player_map_mutex_);
    player_map_mapping_[channel_id] = map_id;
}

void game_server::route_packet_to_map(int channel_id, std::uint16_t cmd, const char* data, std::size_t size) {
    if (!main_thread_manager_) return;
    int map_id = get_player_map(channel_id);
    main_thread_manager_->send_packet_to_map(map_id, channel_id, cmd, data, size);
}

void game_server::aoi_broadcast(int center_channel, std::uint16_t cmd,
                                 const char* data, std::size_t size,
                                 int exclude_channel) {
    if (!main_thread_manager_) return;
    int map_id = get_player_map(center_channel);
    main_thread_manager_->aoi_broadcast(map_id, center_channel, cmd, data, size, exclude_channel);
}

// ── 주기적 DB 플러시 ─────────────────────────────────────────────────────────

static constexpr int kFlushIntervalSec = 30;  // 위치·스탯 자동 저장 주기

void game_server::schedule_flush() {
#ifdef LEMONDORY_HAVE_MYSQL
    if (!flush_timer_ || !player_dao_) return;

    flush_timer_->expires_after(std::chrono::seconds(kFlushIntervalSec));
    flush_timer_->async_wait([this](const asio::error_code& ec) {
        if (ec) return;  // operation_aborted: 서버 종료 중
        flush_players();
        schedule_flush();
    });
#endif
}

void game_server::flush_players() {
#ifdef LEMONDORY_HAVE_MYSQL
    if (!player_dao_) return;

    // 즉시 저장(재화 소비·아이템 거래 등)과 달리,
    // 위치·퀘스트 진행값처럼 손실 허용 데이터만 여기서 처리한다.

    struct Entry {
        std::int64_t db_id;
        float x, y, z;
        int map_id;
        std::optional<db_strand_t> strand;
    };
    std::vector<Entry> entries;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        entries.reserve(channel_to_db_id_.size());

        for (auto& [ch_id, db_id] : channel_to_db_id_) {
            auto eit = channel_to_entity_.find(ch_id);
            auto sit = db_strands_.find(ch_id);
            if (eit == channel_to_entity_.end() || sit == db_strands_.end()) continue;

            Entity entity = eit->second;
            Entry e;
            e.db_id  = db_id;
            e.x = 0.f; e.y = 0.f; e.z = 0.f; e.map_id = 1;
            e.strand = sit->second;

            // ECS 읽기: io_context 스레드에서 실행되므로 락 없이 안전
            if (auto* pos    = game_service().get_component<Position>(entity))
                { e.x = pos->x; e.y = pos->y; e.z = pos->z; }
            if (auto* player = game_service().get_component<Player>(entity))
                e.map_id = static_cast<int>(player->map_id);

            entries.push_back(std::move(e));
        }
    }

    if (entries.empty()) return;

    auto* dao = player_dao_.get();
    for (auto& e : entries) {
        asio::post(*e.strand, [dao, db_id=e.db_id, x=e.x, y=e.y, z=e.z, map_id=e.map_id]() {
            try {
                dao->save_position(db_id, x, y, z, map_id);
            } catch (const std::exception& ex) {
                LOGE("DB thread exception (flush): player_id={} what={}", db_id, ex.what());
            } catch (...) {
                LOGE("DB thread unknown exception (flush): player_id={}", db_id);
            }
        });
    }

    LOGD("Periodic flush: {} players (interval={}s)", entries.size(), kFlushIntervalSec);
#endif
}

} // namespace lemondory::game
