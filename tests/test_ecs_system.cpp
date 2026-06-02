#include "server/game/ecs/game_service.hpp"
#include "server/game/components/transform.hpp"
#include "server/game/components/game_object.hpp"
#include "server/game/components/health.hpp"
// #include "common/log.hpp"  // spdlog 문제로 임시 비활성화
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

using namespace lemondory::game; // 모던 C++ 스타일 네임스페이스

int main() {
    std::cout << "=== ECS System Test ===" << std::endl;
    
    // ECS 게임 서비스 초기화
    if (!game_service().initialize()) {
        std::cerr << "Failed to initialize ECS Game Service!" << std::endl;
        return 1;
    }
    
    std::cout << "ECS Game Service initialized successfully!" << std::endl;
    
    // 플레이어 생성
    std::cout << "\n=== Creating Players ===" << std::endl;
    std::vector<Entity> players;
    
    for (int i = 1; i <= 3; ++i) {
        Entity player = game_service().create_entity();
        
        // 컴포넌트 추가
        game_service().emplace_component<Position>(player, i * 10.0f, 0.0f, 0.0f);
        game_service().emplace_component<Velocity>(player, 0.0f, 0.0f, 0.0f);
        game_service().emplace_component<Player>(player, "Player" + std::to_string(i), i);
        game_service().emplace_component<Health>(player, 100, 50);
        
        players.push_back(player);
        std::cout << "Created Player " << i << " (Entity: " << player << ")" << std::endl;
    }
    
    // 몬스터 생성
    std::cout << "\n=== Creating Monsters ===" << std::endl;
    std::vector<Entity> monsters;
    
    for (int i = 1; i <= 2; ++i) {
        Entity monster = game_service().create_entity();
        
        // 컴포넌트 추가
        game_service().emplace_component<Position>(monster, i * 15.0f, 0.0f, 0.0f);
        game_service().emplace_component<Velocity>(monster, 0.0f, 0.0f, 0.0f);
        game_service().emplace_component<Monster>(monster, i);
        game_service().emplace_component<Health>(monster, 50, 25);
        
        monsters.push_back(monster);
        std::cout << "Created Monster " << i << " (Entity: " << monster << ")" << std::endl;
    }
    
    // 컴포넌트 조회 테스트
    std::cout << "\n=== Component Query Test ===" << std::endl;
    for (Entity player : players) {
        auto* position = game_service().get_component<Position>(player);
        auto* health = game_service().get_component<Health>(player);
        
        if (position && health) {
            std::cout << "Player " << player << " - Position: (" 
                      << position->x << ", " << position->y << ", " << position->z << ")"
                      << " - Health: " << health->hp << "/" << health->max_hp << std::endl;
        }
    }
    
    // 시스템 업데이트 테스트
    std::cout << "\n=== System Update Test ===" << std::endl;
    auto start_time = std::chrono::steady_clock::now();
    
    for (int frame = 0; frame < 60; ++frame) {
        // 60 FPS 시뮬레이션
        game_service().update(0.016f);
        
        if (frame % 10 == 0) {
            std::cout << "Frame " << frame << " - ";
            for (Entity player : players) {
                auto* position = game_service().get_component<Position>(player);
                if (position) {
                    std::cout << "Player " << player << ": (" 
                              << position->x << ", " << position->y << ", " << position->z << ") ";
                }
            }
            std::cout << std::endl;
        }
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(end_time - start_time).count();
    std::cout << "Update loop completed in " << duration << " seconds" << std::endl;
    
    // 통계 출력
    std::cout << "\n=== ECS Statistics ===" << std::endl;
    std::cout << "Total Entities: " << game_service().get_entity_count() << std::endl;
    std::cout << "Total Components: " << game_service().get_component_count() << std::endl;
    std::cout << "Total Systems: " << game_service().get_system_count() << std::endl;
    
    // 엔티티 삭제 테스트
    std::cout << "\n=== Entity Deletion Test ===" << std::endl;
    for (Entity player : players) {
        game_service().destroy_entity(player);
        std::cout << "Destroyed Player " << player << std::endl;
    }
    
    for (Entity monster : monsters) {
        game_service().destroy_entity(monster);
        std::cout << "Destroyed Monster " << monster << std::endl;
    }
    
    // 최종 통계
    std::cout << "\n=== Final Statistics ===" << std::endl;
    std::cout << "Remaining Entities: " << game_service().get_entity_count() << std::endl;
    std::cout << "Remaining Components: " << game_service().get_component_count() << std::endl;
    
    // ECS 게임 서비스 종료
    game_service().shutdown();
    
    std::cout << "=== ECS System Test Completed Successfully! ===" << std::endl;
    return 0;
}
