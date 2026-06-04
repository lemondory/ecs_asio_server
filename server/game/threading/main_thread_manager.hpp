#pragma once

#include "../ecs/game_service.hpp"
#include "map_thread_manager.hpp"
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <functional>

namespace lemondory::game {

// 메인 스레드에서 처리할 작업
struct MainThreadTask {
    std::function<void()> task;
    int priority = 0;  // 우선순위 (낮을수록 높은 우선순위)
    
    MainThreadTask(std::function<void()> t, int p = 0) 
        : task(std::move(t)), priority(p) {}
    
    bool operator<(const MainThreadTask& other) const {
        return priority > other.priority;  // 우선순위 큐용
    }
};

class MainThreadManager {
public:
    MainThreadManager();
    ~MainThreadManager();
    
    // 메인 스레드 시작/종료
    void start(int tick_ms = 16);
    void stop();
    
    // 작업 추가
    void add_task(std::function<void()> task, int priority = 0);
    void add_network_task(std::function<void()> task);
    void add_global_task(std::function<void()> task);
    
    // 맵 스레드 관리
    void create_map_thread(int map_id, const std::string& map_name,
                          float width = 1000.0f, float height = 1000.0f,
                          int max_players = 100, int max_monsters = 50,
                          float spawn_interval = 30.0f, bool monster_spawn = true,
                          int tick_ms = 16);
    void destroy_map_thread(int map_id);
    void send_packet_to_map(int map_id, int channel_id, std::uint16_t cmd, 
                          const char* data, std::size_t size);
    
    // 글로벌 시스템
    void handle_global_chat(int channel_id, const std::string& message);
    void handle_guild_system(int channel_id, const std::string& message);
    void handle_friend_system(int channel_id, const std::string& message);
    
    // 플레이어 퇴장 정리 (on_close에서 호출 — MapThread AOI/셀 정보 제거)
    void remove_player(int channel_id);

    // AOI 브로드캐스트 콜백 등록 — MapThreadManager를 통해 모든 MapThread에 배포
    void set_sender(SenderFn fn);

    // 맵 이동 처리
    void handle_map_transfer(int channel_id, int from_map, int to_map, Entity player_entity);
    
    // ECS 업데이트
    void update_ecs(float delta_time);
    
    // 작업 큐 처리
    void process_task_queue();
    
    // 상태 확인
    bool is_running() const { return running_; }
    int get_map_thread_count() const;
    
private:
    void main_loop();
    void process_network_events();
    void process_global_systems();
    void process_map_transfers();
    
    // 작업 큐
    std::priority_queue<MainThreadTask> task_queue_;
    std::mutex task_mutex_;
    std::condition_variable task_cv_;
    
    // 맵 스레드 관리자
    std::unique_ptr<MapThreadManager> map_thread_manager_;
    
    // 스레드 상태
    std::atomic<bool> running_{false};
    std::thread main_thread_;
    int tick_ms_{16};
    
    // ECS 서비스
    GameService& game_service_;
    
    // 네트워크 관련
    std::queue<std::function<void()>> network_tasks_;
    std::mutex network_mutex_;
    
    // 글로벌 시스템
    std::queue<std::function<void()>> global_tasks_;
    std::mutex global_mutex_;
};

} // namespace lemondory::game
