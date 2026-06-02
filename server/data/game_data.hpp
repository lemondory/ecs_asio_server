#pragma once
#include <string>
#include <cstdint>

namespace lemondory::data {

struct MapData {
    int         id             = 0;
    std::string name;
    std::string type           = "field";  // field | dungeon | town | pvp | raid
    float       width          = 1000.0f;
    float       height         = 1000.0f;
    int         max_players    = 100;
    int         max_monsters   = 50;
    float       spawn_interval = 30.0f;
    bool        monster_spawn  = true;
};

struct MonsterData {
    uint32_t    id             = 0;
    std::string name;
    int         hp             = 100;
    int         atk            = 10;
    int         def            = 5;
    float       detect_range   = 15.0f;
    float       attack_range   = 2.0f;
    float       chase_range    = 20.0f;
    uint32_t    exp_reward     = 0;
};

struct MonsterSpawnData {
    int      map_id     = 0;
    uint32_t monster_id = 0;
    int      weight     = 1;   // 상대적 스폰 확률 가중치
};

struct PlayerLevelData {
    uint32_t level        = 1;
    int      hp           = 100;
    int      mp           = 50;
    int      atk          = 10;
    int      def          = 5;
    uint32_t exp_required = 0;
};

} // namespace lemondory::data
