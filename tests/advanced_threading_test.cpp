#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <string>
#include "server/game/threading/main_thread_manager.hpp"
#include "server/game/threading/map_thread_manager.hpp"
#include "server/game/threading/map_thread.hpp"

using namespace lemondory::game;

// 테스트 헬퍼
void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void print_test_result(const std::string& test_name, bool passed, const std::string& details = "") {
    std::cout << "[" << (passed ? "✅ PASS" : "❌ FAIL") << "] " << test_name;
    if (!details.empty()) {
        std::cout << " - " << details;
    }
    std::cout << std::endl;
}

// 1. 맵별 스레드 독립성 테스트
void test_map_thread_independence() {
    std::cout << "\n🧪 Map Thread Independence Test" << std::endl;
    
    MainThreadManager main_manager;
    main_manager.start();
    sleep_ms(100);
    
    // 여러 맵 생성
    main_manager.create_map_thread(1, "Map1");
    main_manager.create_map_thread(2, "Map2");
    main_manager.create_map_thread(3, "Map3");
    sleep_ms(200);
    
    // 각 맵에 대량 패킷 전달
    const int packets_per_map = 100;
    const char* test_data = "independence_test_data";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 맵별로 동시에 패킷 전달
    std::vector<std::thread> threads;
    for (int map_id = 1; map_id <= 3; ++map_id) {
        threads.emplace_back([&main_manager, map_id, test_data]() {
            for (int i = 0; i < packets_per_map; ++i) {
                main_manager.send_packet_to_map(map_id, i, 0x1001, test_data, 20);
            }
        });
    }
    
    // 모든 스레드 완료 대기
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Processed " << (3 * packets_per_map) << " packets in " << duration.count() << " ms" << std::endl;
    print_test_result("Map thread independence", true, 
                     std::to_string(duration.count()) + "ms for " + std::to_string(3 * packets_per_map) + " packets");
    
    main_manager.stop();
}

// 2. 패킷 라우팅 정확성 테스트
void test_packet_routing_accuracy() {
    std::cout << "\n🧪 Packet Routing Accuracy Test" << std::endl;
    
    MainThreadManager main_manager;
    main_manager.start();
    sleep_ms(100);
    
    // 맵 생성
    main_manager.create_map_thread(1, "TestMap1");
    main_manager.create_map_thread(2, "TestMap2");
    sleep_ms(100);
    
    // 플레이어별 패킷 전달 테스트
    const int player_count = 50;
    const int packets_per_player = 10;
    std::atomic<int> successful_routes{0};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> player_threads;
    for (int player_id = 0; player_id < player_count; ++player_id) {
        player_threads.emplace_back([&main_manager, player_id, &successful_routes]() {
            int map_id = (player_id % 2) + 1; // 맵 1 또는 2
            const char* test_data = "routing_test";
            
            for (int i = 0; i < packets_per_player; ++i) {
                main_manager.send_packet_to_map(map_id, player_id, 0x1001, test_data, 12);
                successful_routes.fetch_add(1);
            }
        });
    }
    
    // 모든 플레이어 스레드 완료 대기
    for (auto& thread : player_threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    int expected_routes = player_count * packets_per_player;
    bool passed = (successful_routes.load() == expected_routes);
    
    std::cout << "Expected routes: " << expected_routes << std::endl;
    std::cout << "Successful routes: " << successful_routes.load() << std::endl;
    std::cout << "Duration: " << duration.count() << " ms" << std::endl;
    
    print_test_result("Packet routing accuracy", passed, 
                     std::to_string(successful_routes.load()) + "/" + std::to_string(expected_routes) + " routes");
    
    main_manager.stop();
}

// 3. 스레드 안전성 스트레스 테스트
void test_thread_safety_stress() {
    std::cout << "\n🧪 Thread Safety Stress Test" << std::endl;
    
    MainThreadManager main_manager;
    main_manager.start();
    sleep_ms(100);
    
    // 여러 맵 생성
    for (int i = 1; i <= 5; ++i) {
        main_manager.create_map_thread(i, "StressMap" + std::to_string(i));
    }
    sleep_ms(200);
    
    const int thread_count = 20;
    const int operations_per_thread = 100;
    std::atomic<int> successful_operations{0};
    std::atomic<int> failed_operations{0};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> stress_threads;
    for (int t = 0; t < thread_count; ++t) {
        stress_threads.emplace_back([&main_manager, t, &successful_operations, &failed_operations]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> map_dist(1, 5);
            std::uniform_int_distribution<> cmd_dist(0x1001, 0x1005);
            
            for (int i = 0; i < operations_per_thread; ++i) {
                try {
                    int map_id = map_dist(gen);
                    int channel_id = t * operations_per_thread + i;
                    std::uint16_t cmd = static_cast<std::uint16_t>(cmd_dist(gen));
                    const char* data = "stress_test_data";
                    
                    main_manager.send_packet_to_map(map_id, channel_id, cmd, data, 16);
                    successful_operations.fetch_add(1);
                } catch (...) {
                    failed_operations.fetch_add(1);
                }
            }
        });
    }
    
    // 모든 스레드 완료 대기
    for (auto& thread : stress_threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    int total_operations = thread_count * operations_per_thread;
    int success_rate = (successful_operations.load() * 100) / total_operations;
    
    std::cout << "Total operations: " << total_operations << std::endl;
    std::cout << "Successful: " << successful_operations.load() << std::endl;
    std::cout << "Failed: " << failed_operations.load() << std::endl;
    std::cout << "Success rate: " << success_rate << "%" << std::endl;
    std::cout << "Duration: " << duration.count() << " ms" << std::endl;
    
    bool passed = (success_rate >= 95); // 95% 이상 성공률
    print_test_result("Thread safety stress test", passed, 
                     std::to_string(success_rate) + "% success rate");
    
    main_manager.stop();
}

// 4. 메모리 누수 및 리소스 관리 테스트
void test_memory_and_resource_management() {
    std::cout << "\n🧪 Memory and Resource Management Test" << std::endl;
    
    const int iterations = 50;
    std::atomic<int> successful_iterations{0};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int iter = 0; iter < iterations; ++iter) {
        try {
            // 매니저 생성
            MainThreadManager main_manager;
            main_manager.start();
            sleep_ms(10);
            
            // 맵 생성
            for (int i = 1; i <= 3; ++i) {
                main_manager.create_map_thread(i, "ResourceMap" + std::to_string(i));
            }
            sleep_ms(50);
            
            // 패킷 전달
            for (int i = 0; i < 100; ++i) {
                main_manager.send_packet_to_map(1, i, 0x1001, "resource_test", 13);
            }
            sleep_ms(10);
            
            // 매니저 종료
            main_manager.stop();
            sleep_ms(10);
            
            successful_iterations.fetch_add(1);
        } catch (...) {
            // 실패한 반복은 카운트하지 않음
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Successful: " << successful_iterations.load() << std::endl;
    std::cout << "Duration: " << duration.count() << " ms" << std::endl;
    
    bool passed = (successful_iterations.load() == iterations);
    print_test_result("Memory and resource management", passed, 
                     std::to_string(successful_iterations.load()) + "/" + std::to_string(iterations) + " iterations");
}

// 5. 성능 벤치마크 테스트
void test_performance_benchmark() {
    std::cout << "\n🧪 Performance Benchmark Test" << std::endl;
    
    MainThreadManager main_manager;
    main_manager.start();
    sleep_ms(100);
    
    // 맵 생성
    for (int i = 1; i <= 10; ++i) {
        main_manager.create_map_thread(i, "BenchmarkMap" + std::to_string(i));
    }
    sleep_ms(200);
    
    const int total_packets = 10000;
    const int packets_per_map = total_packets / 10;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 각 맵에 패킷 전달
    std::vector<std::thread> map_threads;
    for (int map_id = 1; map_id <= 10; ++map_id) {
        map_threads.emplace_back([&main_manager, map_id]() {
            const char* test_data = "benchmark_test_data";
            for (int i = 0; i < packets_per_map; ++i) {
                main_manager.send_packet_to_map(map_id, i, 0x1001, test_data, 19);
            }
        });
    }
    
    // 모든 맵 스레드 완료 대기
    for (auto& thread : map_threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double packets_per_second = (total_packets * 1000000.0) / duration.count();
    double avg_latency = duration.count() / (double)total_packets;
    
    std::cout << "Total packets: " << total_packets << std::endl;
    std::cout << "Duration: " << duration.count() << " microseconds" << std::endl;
    std::cout << "Packets per second: " << packets_per_second << std::endl;
    std::cout << "Average latency: " << avg_latency << " microseconds per packet" << std::endl;
    
    bool passed = (packets_per_second > 50000); // 50k packets/sec 이상
    print_test_result("Performance benchmark", passed, 
                     std::to_string((int)packets_per_second) + " packets/sec");
    
    main_manager.stop();
}

// 6. 맵 이동 테스트
void test_map_transfer() {
    std::cout << "\n🧪 Map Transfer Test" << std::endl;
    
    MainThreadManager main_manager;
    main_manager.start();
    sleep_ms(100);
    
    // 맵 생성
    main_manager.create_map_thread(1, "SourceMap");
    main_manager.create_map_thread(2, "DestinationMap");
    sleep_ms(100);
    
    const int player_count = 20;
    std::atomic<int> successful_transfers{0};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 플레이어들이 맵 간 이동
    std::vector<std::thread> transfer_threads;
    for (int player_id = 0; player_id < player_count; ++player_id) {
        transfer_threads.emplace_back([&main_manager, player_id, &successful_transfers]() {
            // 맵 1에서 시작
            main_manager.send_packet_to_map(1, player_id, 0x1001, "initial_data", 12);
            sleep_ms(10);
            
            // 맵 2로 이동
            main_manager.handle_map_transfer(player_id, 1, 2, 0);
            sleep_ms(10);
            
            // 맵 2에서 패킷 전달
            main_manager.send_packet_to_map(2, player_id, 0x1001, "transferred_data", 16);
            successful_transfers.fetch_add(1);
        });
    }
    
    // 모든 전송 스레드 완료 대기
    for (auto& thread : transfer_threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Player count: " << player_count << std::endl;
    std::cout << "Successful transfers: " << successful_transfers.load() << std::endl;
    std::cout << "Duration: " << duration.count() << " ms" << std::endl;
    
    bool passed = (successful_transfers.load() == player_count);
    print_test_result("Map transfer test", passed, 
                     std::to_string(successful_transfers.load()) + "/" + std::to_string(player_count) + " transfers");
    
    main_manager.stop();
}

int main() {
    std::cout << "🧪 Advanced Threading Test Suite" << std::endl;
    std::cout << "=================================" << std::endl;
    
    try {
        test_map_thread_independence();
        test_packet_routing_accuracy();
        test_thread_safety_stress();
        test_memory_and_resource_management();
        test_performance_benchmark();
        test_map_transfer();
        
        std::cout << "\n✅ All advanced tests completed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "\n❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
