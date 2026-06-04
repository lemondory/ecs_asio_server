#pragma once

#include "../ecs/game_service.hpp"
#include "../components/map.hpp"
#include "../components/transform.hpp"
#include "../components/game_object.hpp"
#include "aoi_grid.hpp"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace lemondory::game {

// 채널에 프레이밍 없이 페이로드를 전송하는 콜백.
// game_server에서 등록하며, MapThread는 채널/네트워크 세부사항 불필요.
using SenderFn = std::function<void(int channel_id, std::uint16_t cmd,
                                    const char* payload, std::size_t size)>;

// 맵 스레드에서 처리할 패킷
struct MapPacket {
    int channel_id;
    std::uint16_t cmd;
    std::string data;
    std::chrono::steady_clock::time_point timestamp;
    
    MapPacket(int ch_id, std::uint16_t c, const char* d, std::size_t size)
        : channel_id(ch_id), cmd(c), data(d, size), 
          timestamp(std::chrono::steady_clock::now()) {}
};

// 맵 스레드에서 처리할 작업
struct MapTask {
    std::function<void()> task;
    int priority = 0;
    
    MapTask(std::function<void()> t, int p = 0) 
        : task(std::move(t)), priority(p) {}
    
    bool operator<(const MapTask& other) const {
        return priority > other.priority;
    }
};

class MapThread {
public:
    MapThread(int map_id, const std::string& map_name,
              float width, float height, int max_players,
              int max_monsters = 50, float spawn_interval = 30.0f,
              bool monster_spawn = true, int tick_ms = 16);
    ~MapThread();
    
    // 스레드 시작/종료
    void start();
    void stop();
    void join();
    
    // 패킷 처리
    void add_packet(int channel_id, std::uint16_t cmd, 
                   const char* data, std::size_t size);
    void process_packets();
    
    // 작업 처리
    void add_task(std::function<void()> task, int priority = 0);
    void process_tasks();
    
    // 플레이어 관리
    void add_player(int channel_id, Entity player_entity);
    void remove_player(int channel_id);
    void update_player(int channel_id, const std::string& data);
    
    // 맵 업데이트
    void update_map(float delta_time);
    void update_players(float delta_time);
    void update_monsters(float delta_time);
    void update_npcs(float delta_time);
    void update_items(float delta_time);
    
    // 맵 이벤트 처리
    void handle_spawn_events(float delta_time);
    void handle_combat_events(float delta_time);
    void handle_interaction_events(float delta_time);
    
    // 상태 조회
    bool is_running() const { return running_; }
    int get_map_id() const { return map_id_; }
    int get_player_count() const { return player_count_.load(); }
    int get_monster_count() const { return monster_count_.load(); }
    int get_npc_count() const { return npc_count_.load(); }
    int get_item_count() const { return item_count_.load(); }
    int get_total_object_count() const { 
        return player_count_.load() + monster_count_.load() + 
               npc_count_.load() + item_count_.load(); 
    }
    
    // 맵 정보
    Entity get_map_entity() const { return map_entity_; }
    const std::string& get_map_name() const { return map_name_; }

    // 브로드캐스트 콜백 등록 (game_server에서 호출)
    void set_sender(SenderFn fn) { sender_ = std::move(fn); }

private:
    void thread_loop();
    void process_single_packet(const MapPacket& packet);
    void handle_movement_packet(int channel_id, const std::string& data);
    void handle_combat_packet(int channel_id, const std::string& data);
    void handle_interaction_packet(int channel_id, const std::string& data);
    void handle_chat_packet(int channel_id, const std::string& data);
    
    // 맵 정보
    int map_id_;
    std::string map_name_;
    Entity map_entity_;
    float width_, height_;
    int max_players_;
    
    // 스레드 상태
    std::atomic<bool> running_{false};
    std::thread thread_;
    
    // 패킷 큐
    std::queue<MapPacket> packet_queue_;
    std::mutex packet_mutex_;
    std::condition_variable packet_cv_;
    
    // 작업 큐
    std::priority_queue<MapTask> task_queue_;
    std::mutex task_mutex_;
    std::condition_variable task_cv_;
    
    // 맵 내 오브젝트들
    std::unordered_set<int> local_players_;      // channel_id
    std::unordered_set<Entity> local_monsters_;  // monster entities
    std::unordered_set<Entity> local_npcs_;      // npc entities
    std::unordered_set<Entity> local_items_;     // item entities
    
    // 오브젝트 관리 뮤텍스
    mutable std::mutex objects_mutex_;
    
    // 성능을 위한 atomic 카운터들
    std::atomic<int> player_count_{0};
    std::atomic<int> monster_count_{0};
    std::atomic<int> npc_count_{0};
    std::atomic<int> item_count_{0};
    
    // 맵 전용 ECS 인스턴스 — 몬스터/NPC/아이템 등 맵-로컬 엔티티 소유
    // 플레이어 엔티티는 game_server의 글로벌 GameService에서 관리
    GameService game_service_local_;
    
    // 맵 컴포넌트 참조
    MapComponent* map_component_;
    MapObjectsComponent* map_objects_component_;
    MapBoundsComponent* map_bounds_component_;
    
    // 맵 내 오브젝트 업데이트 헬퍼
    void update_object_position(Entity entity, float delta_time);
    void update_object_ai(Entity entity, float delta_time);
    void check_object_collisions(Entity entity);
    void handle_object_interactions(Entity entity);
    
    // 맵 경계 검사
    bool is_object_in_bounds(Entity entity);
    void clamp_object_to_bounds(Entity entity);
    
    // 스폰 설정 (data/Map.csv에서 로드)
    int   max_monsters_{50};
    float spawn_interval_{30.0f};
    bool  monster_spawn_{true};
    float spawn_timer_{0.0f};
    std::uint32_t spawn_seed_{42};

    // tick 설정 (server_config.tick_rate에서 계산)
    int tick_ms_{16};

    // AOI 시야 시스템
    AoiGrid aoi_grid_;
    SenderFn sender_;

    // 플레이어 위치 캐시 (MapThread 내부 추적용 — 글로벌 GameService 접근 불필요)
    struct PlayerPos { float x, y, z, rotation; };
    std::unordered_map<int, Entity>    channel_to_entity_;  // channel_id → Entity
    std::unordered_map<int, CellKey>   player_cell_;        // channel_id → AOI 셀
    std::unordered_map<int, PlayerPos> player_pos_;         // channel_id → 마지막 위치

    // 맵 이벤트 처리 헬퍼
    void spawn_monsters(float delta_time);
    void spawn_items(float delta_time);
    void handle_map_events(float delta_time);
};

} // namespace lemondory::game
