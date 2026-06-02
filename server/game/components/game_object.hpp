#pragma once

#include "../ecs/component.hpp"
#include <string>
#include <cstdint>

namespace lemondory::game {

// 게임 오브젝트 타입
enum class GameObjectType : std::uint8_t {
    PLAYER,
    NPC,
    MONSTER,
    ITEM,
    INTERACTIVE_OBJECT,
    PROJECTILE,
    EFFECT
};

// 게임 오브젝트 컴포넌트
struct GameObject : public Component {
    GameObjectType type;
    std::string name;
    std::uint32_t object_id;
    bool is_active;
    
    GameObject() : type(GameObjectType::PLAYER), object_id(0), is_active(false) {}
    GameObject(GameObjectType type, const std::string& name, std::uint32_t object_id)
        : type(type), name(name), object_id(object_id), is_active(true) {}
    
    std::unique_ptr<Component> clone() const override {
        auto obj = std::make_unique<GameObject>(type, name, object_id);
        obj->is_active = is_active;
        return obj;
    }
};

// 플레이어 컴포넌트
struct Player : public Component {
    std::string player_name;
    std::uint32_t player_id;
    std::uint32_t level;
    std::uint32_t experience;
    std::uint32_t map_id;
    int channel_id;  // 네트워크 채널 ID 추가
    
    Player() : player_id(0), level(1), experience(0), map_id(1), channel_id(-1) {}
    Player(const std::string& name, std::uint32_t id, int channel_id = -1, std::uint32_t map_id = 1)
        : player_name(name), player_id(id), level(1), experience(0), map_id(map_id), channel_id(channel_id) {}
    
    std::unique_ptr<Component> clone() const override {
        auto player = std::make_unique<Player>(player_name, player_id, channel_id, map_id);
        player->level = level;
        player->experience = experience;
        return player;
    }
};

// 몬스터 컴포넌트
struct Monster : public Component {
    std::uint32_t monster_type;
    std::uint32_t level;
    std::uint32_t hp;
    std::uint32_t max_hp;
    std::uint32_t target_entity; // 공격 대상
    
    Monster() : monster_type(0), level(1), hp(100), max_hp(100), target_entity(0) {}
    Monster(std::uint32_t type, std::uint32_t level = 1)
        : monster_type(type), level(level), hp(100), max_hp(100), target_entity(0) {}
    
    std::unique_ptr<Component> clone() const override {
        auto monster = std::make_unique<Monster>(monster_type, level);
        monster->hp = hp;
        monster->max_hp = max_hp;
        monster->target_entity = target_entity;
        return monster;
    }
};

// NPC 컴포넌트
struct NPC : public Component {
    std::uint32_t npc_type;
    std::string npc_name;
    std::uint32_t level;
    bool is_interactable;
    
    NPC() : npc_type(0), level(1), is_interactable(true) {}
    NPC(std::uint32_t type, const std::string& name)
        : npc_type(type), npc_name(name), level(1), is_interactable(true) {}
    
    std::unique_ptr<Component> clone() const override {
        auto npc = std::make_unique<NPC>(npc_type, npc_name);
        npc->level = level;
        npc->is_interactable = is_interactable;
        return npc;
    }
};

// 아이템 컴포넌트
struct Item : public Component {
    std::uint32_t item_id;
    std::string item_name;
    std::uint32_t item_type;
    std::uint32_t quantity;
    bool is_pickupable;
    
    Item() : item_id(0), item_type(0), quantity(1), is_pickupable(true) {}
    Item(std::uint32_t id, const std::string& name, std::uint32_t type)
        : item_id(id), item_name(name), item_type(type), quantity(1), is_pickupable(true) {}
    
    std::unique_ptr<Component> clone() const override {
        auto item = std::make_unique<Item>(item_id, item_name, item_type);
        item->quantity = quantity;
        item->is_pickupable = is_pickupable;
        return item;
    }
};

} // namespace lemondory::game
