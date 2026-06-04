#include "client/game_bot.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

int main(int argc, char* argv[]) {
    const std::string host      = (argc > 1) ? argv[1] : "127.0.0.1";
    const int         port      = (argc > 2) ? std::stoi(argv[2]) : 12345;
    const int         bot_count = (argc > 3) ? std::stoi(argv[3]) : 1;
    const int         moves     = (argc > 4) ? std::stoi(argv[4]) : 10;

    std::cout << "[main] Spawning " << bot_count << " bot(s) → "
              << host << ":" << port << "  moves=" << moves << "\n";

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(bot_count));

    for (int i = 0; i < bot_count; ++i) {
        threads.emplace_back([&, i]() {
            // 봇마다 접속 시간을 살짝 엇갈림 — 동시 연결 폭주 방지
            std::this_thread::sleep_for(std::chrono::milliseconds(150 * i));

            const std::string name = "Bot_" + std::to_string(i + 1);
            lemondory::client::GameBot bot(name, host, port);

            if (!bot.connect()) {
                std::cerr << "[" << name << "] 접속 실패\n";
                return;
            }

            bot.run(moves);
            bot.disconnect();
        });
    }

    for (auto& t : threads) t.join();
    return 0;
}
