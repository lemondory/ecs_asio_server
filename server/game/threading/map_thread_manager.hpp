#pragma once

#include "../ecs/game_service.hpp"
#include "map_thread.hpp"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>

namespace lemondory::game {

class MapThreadManager {
public:
    MapThreadManager();
    ~MapThreadManager();
    
    // 맵 스레드 생성/삭제
    void create_map_thread(int map_id, const std::string& map_name,
                          float width, float height, int max_players = 100,
                          int max_monsters = 50, float spawn_interval = 30.0f,
                          bool monster_spawn = true, int tick_ms = 16);
    void destroy_map_thread(int map_id);
    
    // 패킷 전달
    void send_packet_to_map(int map_id, int channel_id, std::uint16_t cmd, 
                          const char* data, std::size_t size);
    
    // 플레이어 관리
    void add_player_to_map(int map_id, int channel_id, Entity player_entity);
    void remove_player_from_map(int map_id, int channel_id);
    void move_player_to_map(int channel_id, int from_map, int to_map, Entity player_entity);
    
    // 맵 상태 조회
    bool is_map_thread_running(int map_id) const;
    int get_map_player_count(int map_id) const;
    std::vector<int> get_active_map_ids() const;
    
    // 전체 맵 스레드 관리
    void stop_all_map_threads();
    void update_all_map_threads(float delta_time);

    // AOI 브로드캐스트 콜백 등록 — 생성된 모든 맵 스레드에 배포
    void set_sender(SenderFn fn);

    // center_channel 기준 AOI 범위 브로드캐스트
    void aoi_broadcast(int map_id, int center_channel, std::uint16_t cmd,
                       const char* data, std::size_t size,
                       int exclude_channel = -1);

    // 플레이어가 현재 속한 map_id 조회 (-1 = 미배정)
    int get_player_map(int channel_id) const;

private:
    SenderFn sender_fn_; // 새 맵 스레드 생성 시 자동 주입
    // 맵 스레드 저장소
    std::unordered_map<int, std::unique_ptr<MapThread>> map_threads_;
    mutable std::mutex map_threads_mutex_;
    
    // 맵 스레드 생성/삭제 헬퍼
    void start_map_thread(int map_id, const std::string& map_name,
                         float width, float height, int max_players,
                         int max_monsters, float spawn_interval, bool monster_spawn,
                         int tick_ms);
    void stop_map_thread(int map_id);
    
    // 플레이어 맵 정보 관리
    std::unordered_map<int, int> player_map_mapping_;  // channel_id -> map_id
    mutable std::mutex player_map_mutex_;
    
    void update_player_map(int channel_id, int map_id);
};

} // namespace lemondory::game
