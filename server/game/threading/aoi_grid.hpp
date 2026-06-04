#pragma once
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lemondory::game {

struct CellKey {
    int32_t cx, cz;
    bool operator==(const CellKey& o) const noexcept { return cx == o.cx && cz == o.cz; }
};

} // namespace lemondory::game

namespace std {
template<> struct hash<lemondory::game::CellKey> {
    size_t operator()(const lemondory::game::CellKey& k) const noexcept {
        return std::hash<int64_t>{}((static_cast<int64_t>(k.cx) << 32) |
                                     static_cast<uint32_t>(k.cz));
    }
};
} // namespace std

namespace lemondory::game {

struct AoiCell {
    std::unordered_set<int> players; // channel_id
};

// X-Z 평면 기반 고정 격자 시야 시스템.
// 플레이어가 셀을 이동할 때만 업데이트되므로 매 틱마다 전체 거리 계산 불필요.
class AoiGrid {
public:
    explicit AoiGrid(float cell_size = 100.0f) : cell_size_(cell_size) {}

    CellKey get_cell(float x, float z) const {
        return { static_cast<int32_t>(std::floor(x / cell_size_)),
                 static_cast<int32_t>(std::floor(z / cell_size_)) };
    }

    // 중심 셀 포함 3×3 = 9개 인접 셀
    std::vector<CellKey> get_neighbors(CellKey key) const {
        std::vector<CellKey> result;
        result.reserve(9);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
                result.push_back({ key.cx + dx, key.cz + dz });
        return result;
    }

    void add_player(int channel_id, CellKey key) {
        cells_[key].players.insert(channel_id);
    }

    void remove_player(int channel_id, CellKey key) {
        auto it = cells_.find(key);
        if (it == cells_.end()) return;
        it->second.players.erase(channel_id);
        if (it->second.players.empty())
            cells_.erase(it);
    }

    void move_player(int channel_id, CellKey old_key, CellKey new_key) {
        if (old_key == new_key) return;
        remove_player(channel_id, old_key);
        add_player(channel_id, new_key);
    }

    // center 셀 기준 3×3 인접 셀의 모든 플레이어 channel_id 반환
    std::vector<int> get_players_in_view(CellKey center) const {
        std::vector<int> result;
        for (const auto& neighbor : get_neighbors(center)) {
            auto it = cells_.find(neighbor);
            if (it == cells_.end()) continue;
            for (int ch : it->second.players)
                result.push_back(ch);
        }
        return result;
    }

private:
    float cell_size_;
    std::unordered_map<CellKey, AoiCell> cells_;
};

} // namespace lemondory::game
