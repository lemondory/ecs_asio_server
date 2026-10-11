#include "ai_system.hpp"
#include "../ecs/component.hpp"
#include "../components/transform.hpp"
#include "../components/game_object.hpp"
#include "../components/health.hpp"
#include "../ecs/game_service.hpp"
#include "server/data/data_manager.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace lemondory::game {

// AISystem 구현
void AISystem::update(float delta_time) {
    update_monster_ai(delta_time);
    update_npc_ai(delta_time);
}

void AISystem::set_target(Entity entity, Entity target) {
    auto* monster = component_manager().get_component<Monster>(entity);
    if (monster) {
        monster->target_entity = target;
    }
}

void AISystem::clear_target(Entity entity) {
    set_target(entity, 0);
}

void AISystem::move_towards_target(Entity entity, float speed) {
    auto* position = component_manager().get_component<Position>(entity);
    auto* monster = component_manager().get_component<Monster>(entity);
    
    if (!position || !monster || monster->target_entity == 0) return;
    
    auto* target_position = component_manager().get_component<Position>(monster->target_entity);
    if (!target_position) return;
    
    // 타겟 방향 계산
    float dx = target_position->x - position->x;
    float dy = target_position->y - position->y;
    float dz = target_position->z - position->z;
    float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
    
    if (distance > 0.1f) {
        // 정규화된 방향 벡터
        float dx_norm = dx / distance;
        float dy_norm = dy / distance;
        float dz_norm = dz / distance;
        
        // 속도 설정
        auto* velocity = component_manager().get_component<Velocity>(entity);
        if (!velocity) {
            // Velocity 컴포넌트가 없으면 생성
            component_manager().emplace_component<Velocity>(entity);
            velocity = component_manager().get_component<Velocity>(entity);
        }
        
        if (velocity) {
            velocity->x = dx_norm * speed;
            velocity->y = dy_norm * speed;
            velocity->z = dz_norm * speed;
        }
    }
}

void AISystem::patrol(Entity entity, float /*x*/, float /*y*/, float /*z*/, float range) {
    auto* position = component_manager().get_component<Position>(entity);
    if (!position) return;
    
    // 순찰 범위 내에서 랜덤 이동
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> dis(-1.0f, 1.0f);
    
    float dx = dis(gen);
    float dy = dis(gen);
    float dz = dis(gen);
    
    // 범위 내로 제한
    float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (distance > range) {
        dx = dx / distance * range;
        dy = dy / distance * range;
        dz = dz / distance * range;
    }
    
    // 속도 설정
    auto* velocity = component_manager().get_component<Velocity>(entity);
    if (!velocity) {
        component_manager().emplace_component<Velocity>(entity);
        velocity = component_manager().get_component<Velocity>(entity);
    }

    if (velocity) {
        // TODO(ai-data): 순찰 속도 0.5 하드코딩 → Monster.csv 또는 BattleConst로 이전
        velocity->x = dx * 0.5f; // 순찰 속도
        velocity->y = dy * 0.5f;
        velocity->z = dz * 0.5f;
    }
}

void AISystem::attack_target(Entity entity) {
    auto* monster = component_manager().get_component<Monster>(entity);
    if (!monster || monster->target_entity == 0) return;
    
    // TODO(combat-damage): 고정 데미지 10 → CombatSystem 공식 사용 (Monster.csv atk 반영)
    auto* target_health = component_manager().get_component<Health>(monster->target_entity);
    if (target_health) {
        target_health->take_damage(10); // 고정 데미지
    }
}

void AISystem::update_monster_ai(float delta_time) {
    // 모든 몬스터 AI 업데이트
    auto monsters = component_manager().get_components<Monster>();
    for (auto& [entity, monster] : monsters) {
        update_ai_state(entity, delta_time);
    }
}

void AISystem::update_npc_ai(float /*delta_time*/) {
    // 모든 NPC AI 업데이트
    auto npcs = component_manager().get_components<NPC>();
    for (auto& [entity, npc] : npcs) {
        // NPC는 기본적으로 순찰
        const float range =
            lemondory::data::DataManager::instance().get_const("NPC_PATROL_RANGE", 10.0f);
        patrol(entity, 0.0f, 0.0f, 0.0f, range);
    }
}

void AISystem::find_nearest_target(Entity entity) {
    auto* position = component_manager().get_component<Position>(entity);
    if (!position) return;
    
Entity nearest_target = 0;
    float nearest_distance = std::numeric_limits<float>::max();
    
    // 모든 플레이어 검사
    auto players = component_manager().get_components<Player>();
    for (auto& [player_entity, player] : players) {
        auto* player_position = component_manager().get_component<Position>(player_entity);
        if (!player_position) continue;
        
        float dx = player_position->x - position->x;
        float dy = player_position->y - position->y;
        float dz = player_position->z - position->z;
        float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
        
        // TODO(ai-data): 탐지 거리 20 하드코딩 → Monster.csv detect_range 사용
        if (distance < nearest_distance && distance < 20.0f) { // 20 유닛 내
            nearest_distance = distance;
            nearest_target = player_entity;
        }
    }
    
    if (nearest_target != 0) {
        set_target(entity, nearest_target);
    }
}

void AISystem::update_ai_state(Entity entity, float /*delta_time*/) {
    auto* monster = component_manager().get_component<Monster>(entity);
    if (!monster) return;
    
    // 타겟이 없으면 찾기
    if (monster->target_entity == 0) {
        find_nearest_target(entity);
    } else {
        // 타겟이 있으면 공격 또는 추적
        auto* target_health = component_manager().get_component<Health>(monster->target_entity);
        if (target_health && target_health->is_alive) {
            // 타겟이 살아있으면 공격
            attack_target(entity);
        } else {
            // 타겟이 죽었으면 타겟 초기화
            clear_target(entity);
        }
    }
}

} // namespace lemondory::game
