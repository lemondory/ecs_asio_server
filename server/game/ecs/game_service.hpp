#pragma once

#include "entity.hpp"
#include "component.hpp"
#include "system.hpp"
#include <atomic>
#include <memory>

namespace lemondory::game {

// Game Service - ECS 기반 게임 엔진
class GameService {
public:
    static GameService& instance();
    
    // 초기화 및 종료
    bool initialize();
    void shutdown();
    
    // 매니저 접근
    EntityManager& get_entity_manager();
    ComponentManager& get_component_manager();
    SystemManager& get_system_manager();
    
    // 게임 업데이트 (메인 루프)
    void update(float delta_time);
    
    // Entity 관리
    Entity create_entity();
    void destroy_entity(Entity entity);
    bool is_valid(Entity entity);
    
    // Component 관리
    template<typename T, typename... Args>
    void emplace_component(Entity entity, Args&&... args) {
        component_manager_->emplace_component<T>(entity, std::forward<Args>(args)...);
    }

    // 서버 시작 시 한 번 호출 — 런타임 reallocation 방지
    template<typename T>
    void reserve_component(std::size_t n) {
        component_manager_->get_store<T>().reserve_capacity(n);
    }

    template<typename T>
    void remove_component(Entity entity) {
        component_manager_->remove_component<T>(entity);
    }

    template<typename T>
    T* get_component(Entity entity) {
        return component_manager_->get_component<T>(entity);
    }

    template<typename T>
    bool has_component(Entity entity) const {
        return component_manager_->has_component<T>(entity);
    }

    template<typename T>
    auto get_components() {
        return component_manager_->get_components<T>();
    }
    
    // System 관리
    template<typename T, typename... Args>
    T& register_system(Args&&... args) {
        return system_manager_->register_system<T>(std::forward<Args>(args)...);
    }
    
    template<typename T>
    T* get_system() {
        return system_manager_->get_system<T>();
    }
    
    // 통계
    std::uint32_t get_entity_count() const;
    std::uint32_t get_component_count() const;
    std::uint32_t get_system_count() const;
    
    // 상태
    bool is_initialized() const { return initialized_; }
    
public:
    GameService() = default;
    
    // 매니저들
    std::unique_ptr<EntityManager> entity_manager_;
    std::unique_ptr<ComponentManager> component_manager_;
    std::unique_ptr<SystemManager> system_manager_;
    
    // 상태
    std::atomic<bool> initialized_{false};
    
    // 내부 메서드
    void initialize_managers();
    void shutdown_managers();
};

// 모던 C++ 스타일 - 간단한 전역 함수들
inline GameService& game_service() { return GameService::instance(); }
inline EntityManager& entity_manager() { return game_service().get_entity_manager(); }
inline ComponentManager& component_manager() { return game_service().get_component_manager(); }
inline SystemManager& system_manager() { return game_service().get_system_manager(); }

} // namespace lemondory::game
