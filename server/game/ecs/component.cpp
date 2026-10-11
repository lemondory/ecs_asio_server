#include "component.hpp"
#include <typeindex>

namespace lemondory::game {


// ComponentManager 구현
ComponentManager& ComponentManager::instance() {
    static ComponentManager instance;
    return instance;
}



void ComponentManager::remove_all_components(Entity entity) {
    std::lock_guard<std::mutex> lock(stores_mutex_);
    
    for (auto& [type_index, store] : stores_) {
        // TODO(ecs-cleanup): 각 스토어에서 엔티티 제거
        // 이 부분은 더 정교한 구현이 필요
    }
}

size_t ComponentManager::get_component_count() const {
    std::lock_guard<std::mutex> lock(stores_mutex_);
    
    size_t total = 0;
    for (const auto& [type_index, store] : stores_) {
        // TODO(ecs-cleanup): 각 스토어의 크기 합계
    }
    return total;
}

size_t ComponentManager::get_component_type_count() const {
    std::lock_guard<std::mutex> lock(stores_mutex_);
    return stores_.size();
}

} // namespace lemondory::game
