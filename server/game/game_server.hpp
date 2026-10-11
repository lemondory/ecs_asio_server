#pragma once
#include <unordered_map>
#include <mutex>
#include <memory>
#include <optional>
#include <vector>

#include "common/network/net_handler.hpp"
#include "common/network/socket_channel_base.hpp"
#include "server/network/asio_server.hpp"
#include "common/network/close_function.hpp"
#include "common/network/frame_codec.hpp"
#include "common/network/message_dispatcher.hpp"
#include "common/network/message_ids.hpp"
#include "ecs/game_service.hpp"
#include "components/transform.hpp"
#include "components/game_object.hpp"
#include "components/health.hpp"
#include "threading/main_thread_manager.hpp"
#include "systems/combat_system.hpp"
#include "server/core/server_config.hpp"

#ifdef LEMONDORY_HAVE_MYSQL
#include "server/db/db_manager.hpp"
#include "server/db/dao/player_dao.hpp"
#include "server/db/failure_sink.hpp"
#endif

#ifdef LEMONDORY_HAVE_REDIS
#include "server/db/redis_client.hpp"
#endif

namespace lemondory::game {

class game_server : public lemondory::network::net_handler {
public:
    game_server() = default;
    ~game_server() override = default;

    void set_config(const lemondory::core::ServerConfig& cfg) { config_ = cfg; }

    bool init(asio::io_context& io,
              const lemondory::network::socket_option& opt,
              const std::string& ip, int port);
    void stop();

    // net_handler 구현
    void on_channel_init(lemondory::network::socket_channel_base* channel, bool use_plugin) override;
    void on_channel_destroy(lemondory::network::socket_channel_base* channel) override;
    void on_accept(int channel_id, lemondory::network::socket_channel_base* channel) override;
    void on_close(int channel_id, const void* error, lemondory::network::close_function reason) override;
    //void on_receive(int channel_id, const char* buffer, std::int32_t size) override;
    void on_tick() override;
    void on_message(int channel_id, std::uint16_t cmd, const char* payload, std::int32_t size) override
    {
        dispatcher_.dispatch(cmd, channel_id, payload, static_cast<std::size_t>(size));
    }

    //using msg_handler = std::function<void(int, std::string_view)>;

    // 메시지 핸들러 등록/해제.
    // void register_handler(std::uint16_t msg_id, msg_handler_t fn);
    // void unregister_handler(std::uint16_t msg_id);
    
    // 테스트용 public 메서드
    void send_test_frame(int channel_id, std::uint16_t msg_id, const char* data, std::size_t size);

    // 브로드캐스트 — 연결된 모든/일부 채널에 동일 프레임 전송
    void broadcast(std::uint16_t cmd, const char* data, std::size_t size);
    void broadcast_except(int exclude_channel_id, std::uint16_t cmd, const char* data, std::size_t size);
    
private:
    // 헬퍼
    void send_frame(lemondory::network::socket_channel_base* ch,
                    std::uint16_t msg_id,
                    const char* data, std::size_t size);
    
    // 게임 메시지 핸들러들
    void handle_login(int channel_id, const char* data, std::size_t size);
    void handle_move(int channel_id, const char* data, std::size_t size);
    void handle_chat(int channel_id, const char* data, std::size_t size);
    void handle_attack(int channel_id, const char* data, std::size_t size);
    void handle_map_enter(int channel_id, const char* data, std::size_t size);
    void handle_guild_create(int channel_id, const char* data, std::size_t size);
    void handle_guild_join(int channel_id, const char* data, std::size_t size);
    void handle_guild_leave(int channel_id, const char* data, std::size_t size);

private:
    std::shared_ptr<lemondory::network::asio_server> server_;
    std::mutex mtx_;
    std::unordered_map<int, lemondory::network::socket_channel_base*> channels_;
    std::unordered_map<int, Entity> channel_to_entity_;   // channel_id → ECS Entity (O(1) lookup)

    // 채널별 프레이밍 상태
    //std::unordered_map<int, std::unique_ptr<lemondory::network::frame_codec>> codecs_;

    // message dispatcher
    lemondory::network::message_dispatcher dispatcher_;
    
    // 설정
    lemondory::core::ServerConfig config_;

    // io_context executor (비동기 DB 콜백 복귀용)
    asio::any_io_executor io_exec_;

    // 주기적 DB 플러시 타이머
    std::optional<asio::steady_timer> flush_timer_;

    // ECS 기반 게임 서비스 (config_.use_ecs로 제어)

    // 스레딩 시스템
    std::unique_ptr<MainThreadManager> main_thread_manager_;
    
    // 플레이어 맵 매핑 (channel_id -> map_id)
    std::unordered_map<int, int> player_map_mapping_;
    std::mutex player_map_mutex_;

#ifdef LEMONDORY_HAVE_MYSQL
    std::unique_ptr<lemondory::db::DbManager>   db_manager_;
    std::unique_ptr<lemondory::db::PlayerDao>   player_dao_;
    std::unique_ptr<lemondory::db::FailureSink> failure_sink_;
    std::unordered_map<int, std::int64_t>       channel_to_db_id_;
    // 플레이어별 strand — 같은 플레이어의 DB 작업을 직렬화, 플레이어 간은 병렬
    // 스레드 수는 init()에서 DbConfig.pool_size 와 동기화
    using db_strand_t = asio::strand<asio::thread_pool::executor_type>;
    std::optional<asio::thread_pool>            db_pool_;
    std::unordered_map<int, db_strand_t>        db_strands_;
#endif

#ifdef LEMONDORY_HAVE_REDIS
    std::unique_ptr<lemondory::db::RedisClient> redis_client_;
#endif

    // TODO(guild-db): 인메모리 길드 (DB 연동 전 임시)
    struct GuildData {
        std::uint32_t id;
        std::string name;
        std::string description;
        int master_channel_id;
        std::vector<int> members; // channel_id 목록
    };
    std::unordered_map<std::uint32_t, GuildData> guilds_;
    std::unordered_map<int, std::uint32_t> channel_to_guild_; // channel_id → guild_id
    std::uint32_t next_guild_id_{1};
    std::mutex guild_mutex_;
    
    // 핸들러 등록 (handlers/*.cpp에서 구현)
    void bind_player_handlers();
    void bind_guild_handlers();

    // 주기적 DB 플러시 (위치·스탯 등 지연 허용 데이터)
    void schedule_flush();
    void flush_players();

    // 스레딩 시스템 헬퍼
    void initialize_threading_system();
    void shutdown_threading_system();
    int get_player_map(int channel_id);
    void update_player_map(int channel_id, int map_id);
    void route_packet_to_map(int channel_id, std::uint16_t cmd, const char* data, std::size_t size);
    // center_channel 기준 AOI 범위에만 전송 (맵 전체 broadcast 대신 사용)
    void aoi_broadcast(int center_channel, std::uint16_t cmd, const char* data, std::size_t size,
                       int exclude_channel = -1);
};

} // namespace lemondory::game