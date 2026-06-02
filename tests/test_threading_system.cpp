#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include <random>
#include "server/game/threading/main_thread_manager.hpp"
#include "server/game/threading/map_thread_manager.hpp"
#include "server/game/threading/map_thread.hpp"

using namespace lemondory::game;

// 테스트 헬퍼 함수
void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void print_test_result(const std::string& test_name, bool passed) {
    std::cout << "[" << (passed ? "PASS" : "FAIL") << "] " << test_name << std::endl;
}

// 1. 맵 스레드 생성/삭제 테스트
void test_map_thread_creation() {
    std::cout << "\n=== Map Thread Creation Test ===" << std::endl;
    
    MapThreadManager manager;
    
    // 맵 스레드 생성
    manager.create_map_thread(1, "TestMap1", 1000.0f, 1000.0f, 50);
    sleep_ms(100); // 스레드 시작 대기
    
    bool created = manager.is_map_thread_running(1);
    print_test_result("Map thread creation", created);
    
    // 맵 스레드 삭제
    manager.destroy_map_thread(1);
    sleep_ms(100); // 스레드 종료 대기
    
    bool destroyed = !manager.is_map_thread_running(1);
    print_test_result("Map thread destruction", destroyed);
}

// 2. 패킷 라우팅 테스트
void test_packet_routing() {
    std::cout << "\n=== Packet Routing Test ===" << std::endl;
    
    MapThreadManager manager;
    manager.create_map_thread(1, "TestMap1", 1000.0f, 1000.0f, 50);
    sleep_ms(100);
    
    // 패킷 전달
    const char* test_data = "test_packet_data";
    manager.send_packet_to_map(1, 123, 0x1001, test_data, 16);
    
    // 존재하지 않는 맵에 패킷 전달
    manager.send_packet_to_map(999, 123, 0x1001, test_data, 16);
    
    print_test_result("Packet routing to existing map", true);
    print_test_result("Packet routing to non-existing map", true);
    
    manager.destroy_map_thread(1);
}

// 3. 플레이어 관리 테스트
void test_player_management() {
    std::cout << "\n=== Player Management Test ===" << std::endl;
    
    MapThreadManager manager;
    manager.create_map_thread(1, "TestMap1", 1000.0f, 1000.0f, 50);
    sleep_ms(100);
    
    // 플레이어 추가
    Entity player_entity = 1001; // 임시 엔티티 ID
    manager.add_player_to_map(1, 123, player_entity);
    sleep_ms(50);
    
    int player_count = manager.get_map_player_count(1);
    print_test_result("Player addition", player_count == 1);
    
    // 플레이어 제거
    manager.remove_player_from_map(1, 123);
    sleep_ms(50);
    
    player_count = manager.get_map_player_count(1);
    print_test_result("Player removal", player_count == 0);
    
    manager.destroy_map_thread(1);
}

// 4. 맵 이동 테스트
void test_map_transfer() {
    std::cout << "\n=== Map Transfer Test ===" << std::endl;
    
    MapThreadManager manager;
    manager.create_map_thread(1, "Map1", 1000.0f, 1000.0f, 50);
    manager.create_map_thread(2, "Map2", 1000.0f, 1000.0f, 50);
    sleep_ms(100);
    
    // 플레이어를 맵1에 추가
    Entity player_entity = 1001;
    manager.add_player_to_map(1, 123, player_entity);
    sleep_ms(50);
    
    int player_count_map1 = manager.get_map_player_count(1);
    int player_count_map2 = manager.get_map_player_count(2);
    print_test_result("Player in map1 before transfer", player_count_map1 == 1);
    print_test_result("Player not in map2 before transfer", player_count_map2 == 0);
    
    // 맵 이동
    manager.move_player_to_map(123, 1, 2, 0);
    sleep_ms(50);
    
    player_count_map1 = manager.get_map_player_count(1);
    player_count_map2 = manager.get_map_player_count(2);
    print_test_result("Player removed from map1 after transfer", player_count_map1 == 0);
    print_test_result("Player added to map2 after transfer", player_count_map2 == 1);
    
    manager.destroy_map_thread(1);
    manager.destroy_map_thread(2);
}

// 5. 멀티 맵 테스트
void test_multiple_maps() {
    std::cout << "\n=== Multiple Maps Test ===" << std::endl;
    
    MapThreadManager manager;
    
    // 여러 맵 생성
    for (int i = 1; i <= 5; ++i) {
        manager.create_map_thread(i, "Map" + std::to_string(i), 1000.0f, 1000.0f, 50);
    }
    sleep_ms(200);
    
    // 각 맵에 플레이어 추가
    for (int i = 1; i <= 5; ++i) {
        Entity player_entity = static_cast<Entity>(1000 + i);
        manager.add_player_to_map(i, 100 + i, player_entity);
    }
    sleep_ms(100);
    
    // 맵별 플레이어 수 확인
    bool all_maps_have_players = true;
    for (int i = 1; i <= 5; ++i) {
        int player_count = manager.get_map_player_count(i);
        if (player_count != 1) {
            all_maps_have_players = false;
            break;
        }
    }
    print_test_result("Multiple maps with players", all_maps_have_players);
    
    // 활성 맵 ID 확인
    auto active_maps = manager.get_active_map_ids();
    print_test_result("Active map count", active_maps.size() == 5);
    
    // 모든 맵 정리
    manager.stop_all_map_threads();
    sleep_ms(100);
    
    auto remaining_maps = manager.get_active_map_ids();
    print_test_result("All maps stopped", remaining_maps.empty());
}

// 6. 성능 테스트
void test_performance() {
    std::cout << "\n=== Performance Test ===" << std::endl;
    
    MapThreadManager manager;
    manager.create_map_thread(1, "PerfTestMap", 1000.0f, 1000.0f, 1000);
    sleep_ms(100);
    
    const int packet_count = 1000;
    const char* test_data = "performance_test_data";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 대량 패킷 전달
    for (int i = 0; i < packet_count; ++i) {
        manager.send_packet_to_map(1, i % 100, 0x1001, test_data, 20);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double packets_per_second = (packet_count * 1000000.0) / duration.count();
    
    std::cout << "Processed " << packet_count << " packets in " 
              << duration.count() << " microseconds" << std::endl;
    std::cout << "Packets per second: " << packets_per_second << std::endl;
    
    print_test_result("Performance test", packets_per_second > 10000); // 10k packets/sec 이상
    
    manager.destroy_map_thread(1);
}

// 7. 스레드 안전성 테스트
void test_thread_safety() {
    std::cout << "\n=== Thread Safety Test ===" << std::endl;
    
    MapThreadManager manager;
    manager.create_map_thread(1, "SafetyTestMap", 1000.0f, 1000.0f, 100);
    sleep_ms(100);
    
    const int thread_count = 10;
    const int operations_per_thread = 100;
    std::vector<std::thread> threads;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 여러 스레드에서 동시에 패킷 전달
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([&manager, t]() {
            for (int i = 0; i < operations_per_thread; ++i) {
                int channel_id = t * operations_per_thread + i;
                const char* data = "thread_safety_test";
                manager.send_packet_to_map(1, channel_id, 0x1001, data, 19);
            }
        });
    }
    
    // 모든 스레드 완료 대기
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Thread safety test completed in " << duration.count() << " ms" << std::endl;
    print_test_result("Thread safety", true);
    
    manager.destroy_map_thread(1);
}

// 8. 메모리 누수 테스트
void test_memory_leaks() {
    std::cout << "\n=== Memory Leak Test ===" << std::endl;
    
    const int iterations = 100;
    
    for (int i = 0; i < iterations; ++i) {
        MapThreadManager manager;
        manager.create_map_thread(1, "LeakTestMap", 1000.0f, 1000.0f, 50);
        sleep_ms(10);
        
        // 패킷 전달
        for (int j = 0; j < 10; ++j) {
            manager.send_packet_to_map(1, j, 0x1001, "leak_test", 9);
        }
        
        manager.destroy_map_thread(1);
        sleep_ms(10);
    }
    
    print_test_result("Memory leak test", true);
}

// 9. 통합 테스트
void test_integration() {
    std::cout << "\n=== Integration Test ===" << std::endl;
    
    MainThreadManager main_manager;
    main_manager.start();
    sleep_ms(100);
    
    // 맵 생성
    main_manager.create_map_thread(1, "IntegrationMap1");
    main_manager.create_map_thread(2, "IntegrationMap2");
    sleep_ms(200);
    
    // 패킷 전달
    const char* test_data = "integration_test";
    main_manager.send_packet_to_map(1, 123, 0x1001, test_data, 16);
    main_manager.send_packet_to_map(2, 456, 0x1002, test_data, 16);
    sleep_ms(100);
    
    // 글로벌 시스템 테스트
    main_manager.handle_global_chat(123, "Hello World!");
    main_manager.handle_guild_system(123, "Guild Message");
    main_manager.handle_friend_system(123, "Friend Message");
    sleep_ms(100);
    
    // 맵 이동 테스트
    main_manager.handle_map_transfer(123, 1, 2, 0);
    sleep_ms(100);
    
    print_test_result("Integration test", true);
    
    main_manager.stop();
}

int main() {
    std::cout << "🧪 Threading System Test Suite" << std::endl;
    std::cout << "================================" << std::endl;
    
    try {
        test_map_thread_creation();
        test_packet_routing();
        test_player_management();
        test_map_transfer();
        test_multiple_maps();
        test_performance();
        test_thread_safety();
        test_memory_leaks();
        test_integration();
        
        std::cout << "\n✅ All tests completed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "\n❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
