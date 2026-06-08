#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <filesystem>

// log_dir: 로그 파일 저장 경로 (기본 "logs/")
// max_files: 보존할 로테이션 파일 수
inline void log_init(spdlog::level::level_enum level = spdlog::level::debug,
                     const std::string& log_dir = "logs",
                     std::size_t max_files = 5) {
    std::filesystem::create_directories(log_dir);

    constexpr std::size_t k_max_file_bytes = 10 * 1024 * 1024; // 10MB per file

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_dir + "/server.log", k_max_file_bytes, max_files);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    auto logger = std::make_shared<spdlog::logger>(
        "server",
        spdlog::sinks_init_list{console_sink, file_sink});
    logger->set_level(level);

    spdlog::set_default_logger(logger);
    spdlog::set_level(level);
    spdlog::flush_on(spdlog::level::warn); // warn 이상은 즉시 flush
}

#define LOGI(...) spdlog::info(__VA_ARGS__)
#define LOGW(...) spdlog::warn(__VA_ARGS__)
#define LOGE(...) spdlog::error(__VA_ARGS__)
#define LOGD(...) spdlog::debug(__VA_ARGS__)
#define LOGT(...) spdlog::trace(__VA_ARGS__)
#define LOGC(...) spdlog::critical(__VA_ARGS__)
