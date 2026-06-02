#pragma once

#include "../ecs/component.hpp"
#include <string>
#include <vector>
#include <unordered_set>

namespace lemondory::game {

// 맵 정보 컴포넌트
struct MapComponent : public Component {
    int map_id;
    std::string map_name;
    float width, height;
    int max_players;
    std::string map_data; // 맵 타일 데이터 등
    
    MapComponent() : map_id(0), width(0), height(0), max_players(0) {}
    MapComponent(int id, const std::string& name, float w, float h, int max_players = 100)
        : map_id(id), map_name(name), width(w), height(h), max_players(max_players) {}
    
    std::unique_ptr<Component> clone() const override {
        auto map = std::make_unique<MapComponent>(map_id, map_name, width, height, max_players);
        map->map_data = map_data;
        return map;
    }
};

// 맵 내 오브젝트 관리 컴포넌트
struct MapObjectsComponent : public Component {
    std::unordered_set<Entity> players;
    std::unordered_set<Entity> monsters;
    std::unordered_set<Entity> npcs;
    std::unordered_set<Entity> items;
    
    MapObjectsComponent() = default;
    
    void add_player(Entity entity) { players.insert(entity); }
    void add_monster(Entity entity) { monsters.insert(entity); }
    void add_npc(Entity entity) { npcs.insert(entity); }
    void add_item(Entity entity) { items.insert(entity); }
    
    void remove_player(Entity entity) { players.erase(entity); }
    void remove_monster(Entity entity) { monsters.erase(entity); }
    void remove_npc(Entity entity) { npcs.erase(entity); }
    void remove_item(Entity entity) { items.erase(entity); }
    
    std::unique_ptr<Component> clone() const override {
        auto map_objects = std::make_unique<MapObjectsComponent>();
        map_objects->players = players;
        map_objects->monsters = monsters;
        map_objects->npcs = npcs;
        map_objects->items = items;
        return map_objects;
    }
};

// 맵 경계 컴포넌트
struct MapBoundsComponent : public Component {
    float min_x, min_y, min_z;
    float max_x, max_y, max_z;
    
    MapBoundsComponent() : min_x(0), min_y(0), min_z(0), max_x(0), max_y(0), max_z(0) {}
    MapBoundsComponent(float min_x, float min_y, float min_z,
                      float max_x, float max_y, float max_z)
        : min_x(min_x), min_y(min_y), min_z(min_z),
          max_x(max_x), max_y(max_y), max_z(max_z) {}
    
    bool is_inside(float x, float y, float z) const {
        return x >= min_x && x <= max_x &&
               y >= min_y && y <= max_y &&
               z >= min_z && z <= max_z;
    }
    
    std::unique_ptr<Component> clone() const override {
        return std::make_unique<MapBoundsComponent>(min_x, min_y, min_z, max_x, max_y, max_z);
    }
};

} // namespace lemondory::game

