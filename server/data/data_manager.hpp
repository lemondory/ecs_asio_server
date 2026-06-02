#pragma once
#include "server/data/game_data.hpp"
#include <vector>
#include <unordered_map>
#include <string>

namespace lemondory::data {

// 서버 시작 시 한 번 로드하는 게임 데이터 (맵/몬스터/스폰 테이블)
// CSV 형식: 1행 = 필드명, 2행 = 타입, 3행~ = 데이터
class DataManager {
public:
    static DataManager& instance();

    // data_path 디렉토리의 CSV 파일들을 로드
    bool load(const std::string& data_path = "data");

    const MapData*          get_map(int map_id) const;
    const MonsterData*      get_monster(uint32_t monster_id) const;
    const PlayerLevelData*  get_player_level(uint32_t level) const;

    // name 에 해당하는 전투 상수 조회; 없으면 default_val 반환
    float get_const(const std::string& name, float default_val = 0.0f) const;

    const std::vector<MapData>&         all_maps()     const { return maps_list_; }
    const std::vector<MonsterData>&     all_monsters() const { return monsters_list_; }

    // 해당 맵의 스폰 테이블 (weight 포함)
    std::vector<const MonsterData*> build_spawn_table(int map_id) const;

private:
    DataManager() = default;

    bool load_maps(const std::string& path);
    bool load_monsters(const std::string& path);
    bool load_monster_spawns(const std::string& path);
    bool load_player_levels(const std::string& path);
    bool load_battle_consts(const std::string& path);

    std::vector<MapData>          maps_list_;
    std::vector<MonsterData>      monsters_list_;
    std::vector<MonsterSpawnData> spawns_;
    std::vector<PlayerLevelData>  player_levels_;

    std::unordered_map<int,         const MapData*>         maps_idx_;
    std::unordered_map<uint32_t,    const MonsterData*>     monsters_idx_;
    std::unordered_map<uint32_t,    const PlayerLevelData*> player_levels_idx_;
    std::unordered_map<std::string, float>                  battle_consts_;
};

} // namespace lemondory::data
