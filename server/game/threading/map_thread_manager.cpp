#include "map_thread_manager.hpp"
#include "common/log.hpp"
#include <algorithm>

namespace lemondory::game {

MapThreadManager::MapThreadManager() = default;

MapThreadManager::~MapThreadManager() {
    stop_all_map_threads();
}

void MapThreadManager::create_map_thread(int map_id, const std::string& map_name,
                                       float width, float height, int max_players,
                                       int max_monsters, float spawn_interval,
                                       bool monster_spawn, int tick_ms) {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);

    if (map_threads_.find(map_id) != map_threads_.end()) {
        LOGW("[MapThreadManager] Map thread {} already exists", map_id);
        return;
    }

    auto map_thread = std::make_unique<MapThread>(
        map_id, map_name, width, height, max_players,
        max_monsters, spawn_interval, monster_spawn, tick_ms);
    if (sender_fn_)
        map_thread->set_sender(sender_fn_);

    map_thread->start();

    map_threads_[map_id] = std::move(map_thread);

    LOGI("[MapThreadManager] Created map thread {} ({})", map_id, map_name);
}

void MapThreadManager::destroy_map_thread(int map_id) {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);
    
    auto it = map_threads_.find(map_id);
    if (it != map_threads_.end()) {
        it->second->stop();
        it->second->join();
        map_threads_.erase(it);
        
        LOGI("[MapThreadManager] Destroyed map thread {}", map_id);
    }
}

void MapThreadManager::send_packet_to_map(int map_id, int channel_id, std::uint16_t cmd, 
                                        const char* data, std::size_t size) {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);
    
    auto it = map_threads_.find(map_id);
    if (it != map_threads_.end()) {
        it->second->add_packet(channel_id, cmd, data, size);
    } else {
        LOGW("[MapThreadManager] Map thread {} not found", map_id);
    }
}

void MapThreadManager::add_player_to_map(int map_id, int channel_id, Entity player_entity) {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);
    
    auto it = map_threads_.find(map_id);
    if (it != map_threads_.end()) {
        it->second->add_player(channel_id, player_entity);
        
        // 플레이어 맵 정보 업데이트
        update_player_map(channel_id, map_id);
        
        LOGD("[MapThreadManager] Added player {} to map {}", channel_id, map_id);
    }
}

void MapThreadManager::remove_player_from_map(int map_id, int channel_id) {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);
    
    auto it = map_threads_.find(map_id);
    if (it != map_threads_.end()) {
        it->second->remove_player(channel_id);
        
        // 플레이어 맵 정보 제거
        std::lock_guard<std::mutex> player_lock(player_map_mutex_);
        player_map_mapping_.erase(channel_id);
        
        LOGD("[MapThreadManager] Removed player {} from map {}", channel_id, map_id);
    }
}

void MapThreadManager::move_player_to_map(int channel_id, int from_map, int to_map, Entity player_entity) {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);

    auto from_it = map_threads_.find(from_map);
    if (from_it != map_threads_.end())
        from_it->second->remove_player(channel_id);

    auto to_it = map_threads_.find(to_map);
    if (to_it != map_threads_.end()) {
        to_it->second->add_player(channel_id, player_entity);
        update_player_map(channel_id, to_map);
        LOGD("[MapThreadManager] Moved player {} from map {} to map {}", channel_id, from_map, to_map);
    }
}

bool MapThreadManager::is_map_thread_running(int map_id) const {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);
    
    auto it = map_threads_.find(map_id);
    return it != map_threads_.end() && it->second->is_running();
}

int MapThreadManager::get_map_player_count(int map_id) const {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);
    
    auto it = map_threads_.find(map_id);
    if (it != map_threads_.end()) {
        return it->second->get_player_count();
    }
    return 0;
}

std::vector<int> MapThreadManager::get_active_map_ids() const {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);
    
    std::vector<int> active_maps;
    for (const auto& [map_id, map_thread] : map_threads_) {
        if (map_thread->is_running()) {
            active_maps.push_back(map_id);
        }
    }
    return active_maps;
}

void MapThreadManager::stop_all_map_threads() {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);
    
    for (auto& [map_id, map_thread] : map_threads_) {
        map_thread->stop();
        map_thread->join();
    }
    
    map_threads_.clear();
    LOGI("[MapThreadManager] Stopped all map threads");
}

void MapThreadManager::update_all_map_threads(float delta_time) {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);
    
    for (auto& [map_id, map_thread] : map_threads_) {
        if (map_thread->is_running()) {
            map_thread->update_map(delta_time);
        }
    }
}

int MapThreadManager::get_player_map(int channel_id) const {
    std::lock_guard<std::mutex> lock(player_map_mutex_);
    
    auto it = player_map_mapping_.find(channel_id);
    return it != player_map_mapping_.end() ? it->second : -1;
}

void MapThreadManager::update_player_map(int channel_id, int map_id) {
    std::lock_guard<std::mutex> lock(player_map_mutex_);
    player_map_mapping_[channel_id] = map_id;
}

void MapThreadManager::set_sender(SenderFn fn) {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);
    sender_fn_ = fn;
    for (auto& [map_id, thread] : map_threads_)
        thread->set_sender(fn);
}

void MapThreadManager::aoi_broadcast(int map_id, int center_channel,
                                     std::uint16_t cmd,
                                     const char* data, std::size_t size,
                                     int exclude_channel) {
    std::lock_guard<std::mutex> lock(map_threads_mutex_);
    auto it = map_threads_.find(map_id);
    if (it != map_threads_.end())
        it->second->broadcast_to_nearby(center_channel, cmd, data, size, exclude_channel);
}

} // namespace lemondory::game
