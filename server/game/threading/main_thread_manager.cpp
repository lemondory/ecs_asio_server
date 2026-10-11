#include "main_thread_manager.hpp"
#include "common/log.hpp"
#include <chrono>

namespace lemondory::game {

MainThreadManager::MainThreadManager() 
    : game_service_(GameService::instance()) {
    map_thread_manager_ = std::make_unique<MapThreadManager>();
}

MainThreadManager::~MainThreadManager() {
    stop();
}

void MainThreadManager::start(int tick_ms) {
    if (running_) return;

    tick_ms_ = tick_ms;
    running_ = true;
    main_thread_ = std::thread(&MainThreadManager::main_loop, this);

    LOGI("MainThreadManager started (tick={}ms)", tick_ms_);
}

void MainThreadManager::stop() {
    if (!running_) return;
    
    running_ = false;
    task_cv_.notify_all();
    
    if (main_thread_.joinable()) {
        main_thread_.join();
    }
    
    // 모든 맵 스레드 정리
    if (map_thread_manager_) {
        map_thread_manager_->stop_all_map_threads();
    }
    
    LOGI("MainThreadManager stopped");
}

void MainThreadManager::add_task(std::function<void()> task, int priority) {
    std::lock_guard<std::mutex> lock(task_mutex_);
    task_queue_.emplace(std::move(task), priority);
    task_cv_.notify_one();
}

void MainThreadManager::add_network_task(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(network_mutex_);
    network_tasks_.push(std::move(task));
}

void MainThreadManager::add_global_task(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(global_mutex_);
    global_tasks_.push(std::move(task));
}

void MainThreadManager::create_map_thread(int map_id, const std::string& map_name,
                                         float width, float height, int max_players,
                                         int max_monsters, float spawn_interval,
                                         bool monster_spawn, int tick_ms) {
    if (map_thread_manager_) {
        map_thread_manager_->create_map_thread(
            map_id, map_name, width, height, max_players,
            max_monsters, spawn_interval, monster_spawn, tick_ms);
    }
}

void MainThreadManager::destroy_map_thread(int map_id) {
    if (map_thread_manager_) {
        map_thread_manager_->destroy_map_thread(map_id);
    }
}

void MainThreadManager::send_packet_to_map(int map_id, int channel_id, std::uint16_t cmd,
                                         const char* data, std::size_t size) {
    if (map_thread_manager_)
        map_thread_manager_->send_packet_to_map(map_id, channel_id, cmd, data, size);
}

void MainThreadManager::aoi_broadcast(int map_id, int center_channel,
                                      std::uint16_t cmd,
                                      const char* data, std::size_t size,
                                      int exclude_channel) {
    if (map_thread_manager_)
        map_thread_manager_->aoi_broadcast(map_id, center_channel, cmd, data, size, exclude_channel);
}

void MainThreadManager::handle_global_chat(int channel_id, const std::string& message) {
    add_global_task([this, channel_id, message]() {
        LOGI("[GlobalChat] Player {}: {}", channel_id, message);
        
        // 모든 맵에 브로드캐스트 (cmd=0x1004: chat)
        auto active_maps = map_thread_manager_->get_active_map_ids();
        for (int map_id : active_maps) {
            map_thread_manager_->send_packet_to_map(map_id, channel_id, 0x1004,
                                                  message.c_str(), message.size());
        }
    });
}

void MainThreadManager::handle_guild_system(int channel_id, const std::string& message) {
    add_global_task([channel_id, message]() {
        LOGD("[GuildSystem] Player {}: {}", channel_id, message);
    });
}

void MainThreadManager::handle_friend_system(int channel_id, const std::string& message) {
    add_global_task([channel_id, message]() {
        LOGD("[FriendSystem] Player {}: {}", channel_id, message);
    });
}

void MainThreadManager::handle_map_transfer(int channel_id, int from_map, int to_map, Entity player_entity) {
    add_task([this, channel_id, from_map, to_map, player_entity]() {
        LOGI("[MapTransfer] Player {} from map {} to map {}", channel_id, from_map, to_map);
        if (map_thread_manager_)
            map_thread_manager_->move_player_to_map(channel_id, from_map, to_map, player_entity);
    }, 1);
}

void MainThreadManager::update_ecs(float delta_time) {
    // ECS 업데이트
    game_service_.update(delta_time);
}

int MainThreadManager::get_map_thread_count() const {
    if (map_thread_manager_) {
        return map_thread_manager_->get_active_map_ids().size();
    }
    return 0;
}

void MainThreadManager::main_loop() {
    LOGD("MainThreadManager loop started");
    
    auto last_update = std::chrono::steady_clock::now();
    
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        float delta_time = std::chrono::duration<float>(now - last_update).count();
        last_update = now;
        
        // 1. 네트워크 이벤트 처리
        process_network_events();
        
        // 2. 글로벌 시스템 처리
        process_global_systems();
        
        // 3. 맵 이동 처리
        process_map_transfers();
        
        // 4. ECS 업데이트
        update_ecs(delta_time);
        
        // 5. 작업 큐 처리
        process_task_queue();

        std::this_thread::sleep_for(std::chrono::milliseconds(tick_ms_));
    }
    
    LOGD("MainThreadManager loop ended");
}

void MainThreadManager::process_network_events() {
    std::lock_guard<std::mutex> lock(network_mutex_);
    while (!network_tasks_.empty()) {
        auto task = network_tasks_.front();
        network_tasks_.pop();
        task();
    }
}

void MainThreadManager::process_global_systems() {
    std::lock_guard<std::mutex> lock(global_mutex_);
    while (!global_tasks_.empty()) {
        auto task = global_tasks_.front();
        global_tasks_.pop();
        task();
    }
}

void MainThreadManager::process_map_transfers() {
    // 맵 이동 처리 로직
    // (구체적인 구현은 필요에 따라 추가)
}

void MainThreadManager::process_task_queue() {
    std::unique_lock<std::mutex> lock(task_mutex_);
    while (!task_queue_.empty()) {
        auto task = task_queue_.top();
        task_queue_.pop();
        lock.unlock();
        
        task.task();
        
        lock.lock();
    }
}

void MainThreadManager::remove_player(int channel_id) {
    if (!map_thread_manager_) return;
    const int map_id = map_thread_manager_->get_player_map(channel_id);
    if (map_id != -1)
        map_thread_manager_->remove_player_from_map(map_id, channel_id);
}

void MainThreadManager::set_sender(SenderFn fn) {
    if (map_thread_manager_)
        map_thread_manager_->set_sender(std::move(fn));
}

} // namespace lemondory::game
