#pragma once

#include "../ecs/component.hpp"
#include <cstdint>

namespace lemondory::game {

// 체력 컴포넌트
struct Health : public Component {
    std::uint32_t hp;
    std::uint32_t max_hp;
    std::uint32_t mp;
    std::uint32_t max_mp;
    bool is_alive;
    
    Health(std::uint32_t max_hp = 100, std::uint32_t max_mp = 50)
        : hp(max_hp), max_hp(max_hp), mp(max_mp), max_mp(max_mp), is_alive(true) {}
    
    std::unique_ptr<Component> clone() const override {
        auto health = std::make_unique<Health>(max_hp, max_mp);
        health->hp = hp;
        health->mp = mp;
        health->is_alive = is_alive;
        return health;
    }
    
    // 체력 관련 메서드
    void take_damage(std::uint32_t damage) {
        if (damage >= hp) {
            hp = 0;
            is_alive = false;
        } else {
            hp -= damage;
        }
    }
    
    void heal(std::uint32_t amount) {
        hp = std::min(hp + amount, max_hp);
        if (hp > 0) is_alive = true;
    }
    
    float get_hp_percentage() const {
        return static_cast<float>(hp) / static_cast<float>(max_hp);
    }
    
    float get_mp_percentage() const {
        return static_cast<float>(mp) / static_cast<float>(max_mp);
    }
};

// 상태 컴포넌트
struct Status : public Component {
    std::uint32_t level;
    std::uint32_t experience;
    std::uint32_t gold;
    bool is_in_combat;
    bool is_moving;
    
    Status(std::uint32_t level = 1)
        : level(level), experience(0), gold(0), is_in_combat(false), is_moving(false) {}
    
    std::unique_ptr<Component> clone() const override {
        auto status = std::make_unique<Status>(level);
        status->experience = experience;
        status->gold = gold;
        status->is_in_combat = is_in_combat;
        status->is_moving = is_moving;
        return status;
    }
    
    // 경험치 관련 메서드
    void add_experience(std::uint32_t exp) {
        experience += exp;
        // TODO(level-stats): 레벨업 로직 (PlayerLevel.csv exp_required 사용)
    }
    
    void add_gold(std::uint32_t amount) {
        gold += amount;
    }
    
    bool spend_gold(std::uint32_t amount) {
        if (gold >= amount) {
            gold -= amount;
            return true;
        }
        return false;
    }
};

} // namespace lemondory::game
