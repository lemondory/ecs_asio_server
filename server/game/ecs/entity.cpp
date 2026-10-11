#include "entity.hpp"
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace lemondory::game {

EntityManager& EntityManager::instance() {
    static EntityManager instance;
    return instance;
}

Entity EntityManager::create_entity() {
    std::lock_guard<std::mutex> lock(entities_mutex_);
    
    Entity entity = next_entity_id_.fetch_add(1, std::memory_order_relaxed);
    active_entities_.insert(entity);
    entity_count_.fetch_add(1, std::memory_order_relaxed);
    
    return entity;
}

void EntityManager::destroy_entity(Entity entity) {
    std::lock_guard<std::mutex> lock(entities_mutex_);
    
    auto it = active_entities_.find(entity);
    if (it != active_entities_.end()) {
        active_entities_.erase(it);
        entity_count_.fetch_sub(1, std::memory_order_relaxed);
    }
}

bool EntityManager::is_valid(Entity entity) const {
    std::lock_guard<std::mutex> lock(entities_mutex_);
    return active_entities_.find(entity) != active_entities_.end();
}

std::vector<Entity> EntityManager::get_all_entities() const {
    std::lock_guard<std::mutex> lock(entities_mutex_);
    return std::vector<Entity>(active_entities_.begin(), active_entities_.end());
}

std::vector<Entity> EntityManager::get_entities_in_range(float x, float y, float z, float range) {
    // TODO(spatial-index): 공간 인덱싱 구현 (MapThread AoiGrid 재사용 검토)
    return get_all_entities();
}

std::uint32_t EntityManager::get_entity_count() const {
    return entity_count_.load(std::memory_order_relaxed);
}

std::uint32_t EntityManager::get_next_id() const {
    return next_entity_id_.load(std::memory_order_relaxed);
}

void EntityManager::destroy_entities(const std::vector<Entity>& entities) {
    std::lock_guard<std::mutex> lock(entities_mutex_);
    
    for (Entity entity : entities) {
        auto it = active_entities_.find(entity);
        if (it != active_entities_.end()) {
            active_entities_.erase(it);
            entity_count_.fetch_sub(1, std::memory_order_relaxed);
        }
    }
}

std::vector<Entity> EntityManager::create_entities(std::uint32_t count) {
    std::vector<Entity> entities;
    entities.reserve(count);
    
    std::lock_guard<std::mutex> lock(entities_mutex_);
    
    for (std::uint32_t i = 0; i < count; ++i) {
        Entity entity = next_entity_id_.fetch_add(1, std::memory_order_relaxed);
        active_entities_.insert(entity);
        entities.push_back(entity);
    }
    
    entity_count_.fetch_add(count, std::memory_order_relaxed);
    return entities;
}

} // namespace lemondory::game
