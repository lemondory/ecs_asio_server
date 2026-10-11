#pragma once
#include "protocol_base.hpp"

namespace lemondory::shared {

// ==================== 플레이어 이동 메시지 ====================

struct player_move_req {
    float x, y, z;
    float rotation;
    std::uint32_t timestamp;
    
    std::vector<char> serialize() const {
        std::vector<char> out;
        write_float_le(out, x);
        write_float_le(out, y);
        write_float_le(out, z);
        write_float_le(out, rotation);
        write_le32(out, timestamp);
        return out;
    }

    static bool parse(const char* p, const char* end, player_move_req& out) {
        if (p + 20 > end) return false;
        out.x        = read_float_le(p); p += 4;
        out.y        = read_float_le(p); p += 4;
        out.z        = read_float_le(p); p += 4;
        out.rotation = read_float_le(p); p += 4;
        out.timestamp = read_le32(p);    p += 4;
        return true;
    }
};

struct player_move_res {
    std::uint32_t player_id;
    float x, y, z;
    float rotation;
    std::uint32_t timestamp;
    
    std::vector<char> serialize() const {
        std::vector<char> out;
        write_le32(out, player_id);
        write_float_le(out, x);
        write_float_le(out, y);
        write_float_le(out, z);
        write_float_le(out, rotation);
        write_le32(out, timestamp);
        return out;
    }

    static bool parse(const char* p, const char* end, player_move_res& out) {
        if (p + 24 > end) return false;
        out.player_id = read_le32(p);    p += 4;
        out.x        = read_float_le(p); p += 4;
        out.y        = read_float_le(p); p += 4;
        out.z        = read_float_le(p); p += 4;
        out.rotation = read_float_le(p); p += 4;
        out.timestamp = read_le32(p);    p += 4;
        return true;
    }
};

// ==================== 플레이어 정보 메시지 ====================

struct player_info_req {
    std::uint32_t player_id;
    
    std::vector<char> serialize() const {
        std::vector<char> out;
        write_le32(out, player_id);
        return out;
    }
    
    static bool parse(const char* p, const char* end, player_info_req& out) {
        if (p + 4 > end) return false;
        out.player_id = read_le32(p); p += 4;
        return true;
    }
};

struct player_info_res {
    std::uint32_t player_id;
    std::string player_name;
    std::uint32_t level;
    std::uint32_t experience;
    float x, y, z;
    float rotation;
    std::uint32_t map_id;
    
    std::vector<char> serialize() const {
        std::vector<char> out;
        write_le32(out, player_id);
        write_lp_string(out, player_name);
        write_le32(out, level);
        write_le32(out, experience);
        write_float_le(out, x);
        write_float_le(out, y);
        write_float_le(out, z);
        write_float_le(out, rotation);
        write_le32(out, map_id);
        return out;
    }
    
    static bool parse(const char* p, const char* end, player_info_res& out) {
        if (p + 28 > end) return false;
        out.player_id = read_le32(p); p += 4;
        if (!read_lp_string(p, end, out.player_name)) return false;
        out.level = read_le32(p); p += 4;
        out.experience = read_le32(p); p += 4;
        out.x        = read_float_le(p); p += 4;
        out.y        = read_float_le(p); p += 4;
        out.z        = read_float_le(p); p += 4;
        out.rotation = read_float_le(p); p += 4;
        out.map_id   = read_le32(p);     p += 4;
        return true;
    }
};

// ==================== 플레이어 상태 메시지 ====================

struct player_status_update {
    std::uint32_t player_id;
    std::uint32_t status; // 0: 오프라인, 1: 온라인, 2: 자리비움, 3: 게임중
    std::uint32_t timestamp;
    
    std::vector<char> serialize() const {
        std::vector<char> out;
        write_le32(out, player_id);
        write_le32(out, status);
        write_le32(out, timestamp);
        return out;
    }
    
    static bool parse(const char* p, const char* end, player_status_update& out) {
        if (p + 12 > end) return false;
        out.player_id = read_le32(p); p += 4;
        out.status = read_le32(p); p += 4;
        out.timestamp = read_le32(p); p += 4;
        return true;
    }
};

// ==================== 플레이어 퇴장 알림 (cmd 1007) ====================
// 플레이어가 접속 해제 시 AOI 범위 내 플레이어에게 전송

struct player_leave_notify {
    std::uint32_t channel_id;

    std::vector<char> serialize() const {
        std::vector<char> out;
        write_le32(out, channel_id);
        return out;
    }

    static bool parse(const char* p, const char* end, player_leave_notify& out) {
        if (p + 4 > end) return false;
        out.channel_id = read_le32(p);
        return true;
    }
};

} // namespace lemondory::shared

