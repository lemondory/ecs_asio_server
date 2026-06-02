#include "server/data/data_manager.hpp"
#include "common/log.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace lemondory::data {

DataManager& DataManager::instance() {
    static DataManager inst;
    return inst;
}

// CSV 한 줄을 필드 벡터로 파싱 (따옴표 없는 단순 CSV)
static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\r'))
            tok.erase(tok.begin());
        while (!tok.empty() && (tok.back()  == ' ' || tok.back()  == '\r'))
            tok.pop_back();
        out.push_back(std::move(tok));
    }
    return out;
}

bool DataManager::load(const std::string& data_path) {
    bool ok = true;
    ok &= load_maps(data_path + "/Map.csv");
    ok &= load_monsters(data_path + "/Monster.csv");
    ok &= load_monster_spawns(data_path + "/MonsterSpawn.csv");

    // 인덱스 구축 (list가 완전히 채워진 뒤 포인터 고정)
    ok &= load_player_levels(data_path + "/PlayerLevel.csv");
    ok &= load_battle_consts(data_path + "/BattleConst.csv");

    // 인덱스 구축 (list가 완전히 채워진 뒤 포인터 고정)
    maps_idx_.clear();
    for (const auto& m : maps_list_)
        maps_idx_[m.id] = &m;

    monsters_idx_.clear();
    for (const auto& m : monsters_list_)
        monsters_idx_[m.id] = &m;

    player_levels_idx_.clear();
    for (const auto& p : player_levels_)
        player_levels_idx_[p.level] = &p;

    LOGI("DataManager loaded: {} maps, {} monsters, {} spawn entries, {} player levels, {} battle consts",
         maps_list_.size(), monsters_list_.size(), spawns_.size(),
         player_levels_.size(), battle_consts_.size());
    return ok;
}

bool DataManager::load_maps(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOGW("DataManager: cannot open {}", path);
        return false;
    }

    std::string line;
    int row = 0;
    while (std::getline(f, line)) {
        ++row;
        if (row <= 2) continue;  // 1행=필드명, 2행=타입 스킵
        if (line.empty() || line[0] == '\r') continue;

        auto cols = split_csv(line);
        if (cols.size() < 9) continue;

        MapData m;
        m.id             = std::stoi(cols[0]);
        m.name           = cols[1];
        m.type           = cols[2];
        m.width          = std::stof(cols[3]);
        m.height         = std::stof(cols[4]);
        m.max_players    = std::stoi(cols[5]);
        m.max_monsters   = std::stoi(cols[6]);
        m.spawn_interval = std::stof(cols[7]);
        m.monster_spawn  = (cols[8] != "0" && cols[8] != "false");
        maps_list_.push_back(std::move(m));
    }
    return true;
}

bool DataManager::load_monsters(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOGW("DataManager: cannot open {}", path);
        return false;
    }

    std::string line;
    int row = 0;
    while (std::getline(f, line)) {
        ++row;
        if (row <= 2) continue;
        if (line.empty() || line[0] == '\r') continue;

        auto cols = split_csv(line);
        if (cols.size() < 9) continue;

        MonsterData m;
        m.id           = static_cast<uint32_t>(std::stoul(cols[0]));
        m.name         = cols[1];
        m.hp           = std::stoi(cols[2]);
        m.atk          = std::stoi(cols[3]);
        m.def          = std::stoi(cols[4]);
        m.detect_range = std::stof(cols[5]);
        m.attack_range = std::stof(cols[6]);
        m.chase_range  = std::stof(cols[7]);
        m.exp_reward   = static_cast<uint32_t>(std::stoul(cols[8]));
        monsters_list_.push_back(std::move(m));
    }
    return true;
}

bool DataManager::load_monster_spawns(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOGW("DataManager: cannot open {}", path);
        return false;
    }

    std::string line;
    int row = 0;
    while (std::getline(f, line)) {
        ++row;
        if (row <= 2) continue;
        if (line.empty() || line[0] == '\r') continue;

        auto cols = split_csv(line);
        if (cols.size() < 3) continue;

        MonsterSpawnData s;
        s.map_id     = std::stoi(cols[0]);
        s.monster_id = static_cast<uint32_t>(std::stoul(cols[1]));
        s.weight     = std::stoi(cols[2]);
        spawns_.push_back(s);
    }
    return true;
}

const MapData* DataManager::get_map(int map_id) const {
    auto it = maps_idx_.find(map_id);
    return it != maps_idx_.end() ? it->second : nullptr;
}

const MonsterData* DataManager::get_monster(uint32_t monster_id) const {
    auto it = monsters_idx_.find(monster_id);
    return it != monsters_idx_.end() ? it->second : nullptr;
}

std::vector<const MonsterData*> DataManager::build_spawn_table(int map_id) const {
    std::vector<const MonsterData*> table;
    for (const auto& s : spawns_) {
        if (s.map_id != map_id) continue;
        auto it = monsters_idx_.find(s.monster_id);
        if (it == monsters_idx_.end()) continue;
        for (int i = 0; i < s.weight; ++i)
            table.push_back(it->second);
    }
    return table;
}

const PlayerLevelData* DataManager::get_player_level(uint32_t level) const {
    auto it = player_levels_idx_.find(level);
    return it != player_levels_idx_.end() ? it->second : nullptr;
}

float DataManager::get_const(const std::string& name, float default_val) const {
    auto it = battle_consts_.find(name);
    return it != battle_consts_.end() ? it->second : default_val;
}

bool DataManager::load_player_levels(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOGW("DataManager: cannot open {}", path);
        return false;
    }

    std::string line;
    int row = 0;
    while (std::getline(f, line)) {
        ++row;
        if (row <= 2) continue;
        if (line.empty() || line[0] == '\r') continue;

        auto cols = split_csv(line);
        if (cols.size() < 6) continue;

        PlayerLevelData p;
        p.level        = static_cast<uint32_t>(std::stoul(cols[0]));
        p.hp           = std::stoi(cols[1]);
        p.mp           = std::stoi(cols[2]);
        p.atk          = std::stoi(cols[3]);
        p.def          = std::stoi(cols[4]);
        p.exp_required = static_cast<uint32_t>(std::stoul(cols[5]));
        player_levels_.push_back(std::move(p));
    }
    return true;
}

bool DataManager::load_battle_consts(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOGW("DataManager: cannot open {}", path);
        return false;
    }

    std::string line;
    int row = 0;
    while (std::getline(f, line)) {
        ++row;
        if (row <= 2) continue;
        if (line.empty() || line[0] == '\r') continue;

        auto cols = split_csv(line);
        if (cols.size() < 2) continue;

        battle_consts_[cols[0]] = std::stof(cols[1]);
    }
    return true;
}

} // namespace lemondory::data
