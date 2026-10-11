#include "combat_system.hpp"
#include "../ecs/game_service.hpp"
#include "../components/health.hpp"
#include "../components/game_object.hpp"
#include "common/log.hpp"
#include <iostream>

namespace lemondory::game {

void CombatSystem::update(float /*delta_time*/) {
    // 전투 중인 엔티티들 처리
    auto entities = component_manager().get_entities_with_component<Health>();
    for (Entity entity : entities) {
        auto* health = component_manager().get_component<Health>(entity);
        if (health && health->hp <= 0) {
            LOGD("Entity {} died", static_cast<int>(entity));
            // TODO(respawn): 사망 처리 로직 — 현재 handle_attack에서 hp=0 유지 + 1006 알림만 수행
        }
    }
}

void CombatSystem::attack(Entity attacker, Entity target) {
    LOGD("Entity {} attacks Entity {}", static_cast<int>(attacker), static_cast<int>(target));
    
    // 기본 데미지 계산
    std::uint32_t damage = 10; // TODO(combat-damage): ATK/DEF 기반 계산 (handle_attack과 공식 통합)
    take_damage(target, damage);
}

void CombatSystem::take_damage(Entity entity, std::uint32_t damage) {
    auto* health = component_manager().get_component<Health>(entity);
    if (health) {
        health->hp = (health->hp > damage) ? health->hp - damage : 0;
        LOGD("Entity {} takes {} damage. HP: {}", static_cast<int>(entity), damage, health->hp);
    }
}

void CombatSystem::heal(Entity entity, std::uint32_t amount) {
    auto* health = component_manager().get_component<Health>(entity);
    if (health) {
        health->hp = std::min(health->max_hp, health->hp + amount);
        LOGD("Entity {} healed by {}. HP: {}", static_cast<int>(entity), amount, health->hp);
    }
}

bool CombatSystem::is_in_combat(Entity entity) {
    // 간단한 전투 상태 확인 (HP가 최대값보다 낮으면 전투 중)
    auto* health = component_manager().get_component<Health>(entity);
    return health && health->hp < health->max_hp;
}

Entity CombatSystem::get_target(Entity entity) {
    // 몬스터의 타겟 조회
    auto* monster = component_manager().get_component<Monster>(entity);
    return monster ? monster->target_entity : 0;
}

std::uint32_t CombatSystem::get_hp(Entity entity) {
    auto* health = component_manager().get_component<Health>(entity);
    return health ? health->hp : 0;
}

std::uint32_t CombatSystem::get_max_hp(Entity entity) {
    auto* health = component_manager().get_component<Health>(entity);
    return health ? health->max_hp : 0;
}

} // namespace lemondory::game
