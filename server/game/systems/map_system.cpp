#include "map_system.hpp"
#include "../ecs/game_service.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace lemondory::game {

void MapSystem::update(float delta_time) {
    // 모든 맵 업데이트
    auto maps = game_service().get_components<MapComponent>();
    for (auto& [map_entity, map] : maps) {
        update_map_objects(map_entity, delta_time);
        check_map_collisions(map_entity);
        process_map_events(map_entity, delta_time);
    }
    
    // 오브젝트 캐시 업데이트 (필요시)
    if (cache_dirty_) {
        update_object_cache();
        cache_dirty_ = false;
    }
}

Entity MapSystem::create_map(int map_id, const std::string& map_name, 
                           float width, float height, int max_players) {
    Entity map_entity = game_service().create_entity();
    
    // 맵 컴포넌트 추가
    game_service().emplace_component<MapComponent>(map_entity, map_id, map_name, width, height, max_players);
    game_service().emplace_component<MapObjectsComponent>(map_entity);
    game_service().emplace_component<MapBoundsComponent>(map_entity, 0.f, 0.f, 0.f, width, height, height);
    
    std::cout << "[MapSystem] Created map: " << map_name << " (ID: " << map_id << ")" << std::endl;
    return map_entity;
}

void MapSystem::destroy_map(Entity map_entity) {
    // 맵 내 모든 오브젝트 제거
    auto* map_objects = game_service().get_component<MapObjectsComponent>(map_entity);
    if (map_objects) {
        // 모든 오브젝트 파괴
        for (auto player : map_objects->players) {
            game_service().destroy_entity(player);
        }
        for (auto monster : map_objects->monsters) {
            game_service().destroy_entity(monster);
        }
        for (auto npc : map_objects->npcs) {
            game_service().destroy_entity(npc);
        }
        for (auto item : map_objects->items) {
            game_service().destroy_entity(item);
        }
    }
    
    // 맵 엔티티 파괴
    game_service().destroy_entity(map_entity);
    std::cout << "[MapSystem] Destroyed map entity: " << map_entity << std::endl;
}

void MapSystem::add_object_to_map(Entity map_entity, Entity object_entity) {
    auto* map_objects = game_service().get_component<MapObjectsComponent>(map_entity);
    if (!map_objects) return;
    
    // 오브젝트 타입에 따라 분류
    if (game_service().has_component<Player>(object_entity)) {
        map_objects->add_player(object_entity);
    } else if (game_service().has_component<Monster>(object_entity)) {
        map_objects->add_monster(object_entity);
    } else if (game_service().has_component<NPC>(object_entity)) {
        map_objects->add_npc(object_entity);
    } else if (game_service().has_component<Item>(object_entity)) {
        map_objects->add_item(object_entity);
    }
    
    cache_dirty_ = true;
    std::cout << "[MapSystem] Added object " << object_entity << " to map " << map_entity << std::endl;
}

void MapSystem::remove_object_from_map(Entity map_entity, Entity object_entity) {
    auto* map_objects = game_service().get_component<MapObjectsComponent>(map_entity);
    if (!map_objects) return;
    
    // 오브젝트 타입에 따라 제거
    if (game_service().has_component<Player>(object_entity)) {
        map_objects->remove_player(object_entity);
    } else if (game_service().has_component<Monster>(object_entity)) {
        map_objects->remove_monster(object_entity);
    } else if (game_service().has_component<NPC>(object_entity)) {
        map_objects->remove_npc(object_entity);
    } else if (game_service().has_component<Item>(object_entity)) {
        map_objects->remove_item(object_entity);
    }
    
    cache_dirty_ = true;
    std::cout << "[MapSystem] Removed object " << object_entity << " from map " << map_entity << std::endl;
}

std::vector<Entity> MapSystem::get_objects_in_map(Entity map_entity) {
    std::vector<Entity> objects;
    auto* map_objects = game_service().get_component<MapObjectsComponent>(map_entity);
    if (!map_objects) return objects;
    
    objects.insert(objects.end(), map_objects->players.begin(), map_objects->players.end());
    objects.insert(objects.end(), map_objects->monsters.begin(), map_objects->monsters.end());
    objects.insert(objects.end(), map_objects->npcs.begin(), map_objects->npcs.end());
    objects.insert(objects.end(), map_objects->items.begin(), map_objects->items.end());
    
    return objects;
}

std::vector<Entity> MapSystem::get_objects_in_range(Entity map_entity, float x, float y, float z, float range) {
    std::vector<Entity> objects_in_range;
    auto objects = get_objects_in_map(map_entity);
    
    for (auto object_entity : objects) {
        auto* position = game_service().get_component<Position>(object_entity);
        if (!position) continue;
        
        float distance = static_cast<float>(std::sqrt(
            std::pow(position->x - x, 2) + 
            std::pow(position->y - y, 2) + 
            std::pow(position->z - z, 2)
        ));
        
        if (distance <= range) {
            objects_in_range.push_back(object_entity);
        }
    }
    
    return objects_in_range;
}

bool MapSystem::is_object_in_bounds(Entity map_entity, Entity object_entity) {
    auto* bounds = game_service().get_component<MapBoundsComponent>(map_entity);
    auto* position = game_service().get_component<Position>(object_entity);
    
    if (!bounds || !position) return false;
    
    return bounds->is_inside(position->x, position->y, position->z);
}

void MapSystem::clamp_object_to_bounds(Entity map_entity, Entity object_entity) {
    auto* bounds = game_service().get_component<MapBoundsComponent>(map_entity);
    auto* position = game_service().get_component<Position>(object_entity);
    
    if (!bounds || !position) return;
    
    position->x = std::clamp(position->x, bounds->min_x, bounds->max_x);
    position->y = std::clamp(position->y, bounds->min_y, bounds->max_y);
    position->z = std::clamp(position->z, bounds->min_z, bounds->max_z);
}

int MapSystem::get_player_count(Entity map_entity) {
    auto* map_objects = game_service().get_component<MapObjectsComponent>(map_entity);
    return map_objects ? static_cast<int>(map_objects->players.size()) : 0;
}

int MapSystem::get_monster_count(Entity map_entity) {
    auto* map_objects = game_service().get_component<MapObjectsComponent>(map_entity);
    return map_objects ? static_cast<int>(map_objects->monsters.size()) : 0;
}

int MapSystem::get_total_object_count(Entity map_entity) {
    auto* map_objects = game_service().get_component<MapObjectsComponent>(map_entity);
    if (!map_objects) return 0;
    
    return static_cast<int>(map_objects->players.size() + 
                           map_objects->monsters.size() + 
                           map_objects->npcs.size() + 
                           map_objects->items.size());
}

void MapSystem::update_map_objects(Entity /*map_entity*/, float /*delta_time*/) {
    // 맵 내 모든 오브젝트 업데이트는 다른 시스템들이 담당
    // (MovementSystem, AISystem, CombatSystem 등)
    // 여기서는 맵 레벨의 로직만 처리
}

void MapSystem::check_map_collisions(Entity map_entity) {
    // 맵 레벨 충돌 검사 (예: 오브젝트가 맵 경계를 벗어났는지)
    auto objects = get_objects_in_map(map_entity);
    
    for (auto object_entity : objects) {
        if (!is_object_in_bounds(map_entity, object_entity)) {
            // 경계 밖으로 나간 오브젝트를 경계 내로 되돌림
            clamp_object_to_bounds(map_entity, object_entity);
        }
    }
}

void MapSystem::process_map_events(Entity /*map_entity*/, float /*delta_time*/) {
    // 맵 레벨 이벤트 처리 (예: 맵 전환, 스폰/디스폰 등)
}

void MapSystem::update_object_cache() {
    map_object_cache_.clear();
    
    auto maps = game_service().get_components<MapComponent>();
    for (auto& [map_entity, map] : maps) {
        map_object_cache_[map_entity] = get_objects_in_map(map_entity);
    }
}

} // namespace lemondory::game
