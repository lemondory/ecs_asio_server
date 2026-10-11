#include <asio.hpp>
#include <iostream>
#include <vector>
#include <cstdint>

using namespace asio;
using namespace asio::ip;

// 게임 메시지 ID (서버와 동일)
enum GameMessageID : uint16_t {
    PING = 1,
    PONG = 2,
    LOGIN = 1001,
    MOVE = 1002,
    CHAT = 1003,
    ATTACK = 1004
};

// 프레임 생성 함수 (서버의 frame_codec과 유사)
std::vector<char> create_frame(uint16_t msg_id, const std::string& data) {
    std::vector<char> frame(8 + data.size()); // CRC(4) + MSG_ID(2) + LEN(2) + PAYLOAD
    
    // TODO(crc): CRC32 임시로 0 — 서버 frame_codec과 동일한 CRC 계산 적용
    uint32_t crc = 0;
    std::memcpy(&frame[0], &crc, 4);

    // Message ID
    std::memcpy(&frame[4], &msg_id, 2);

    // Payload Length
    uint16_t data_len = static_cast<uint16_t>(data.size());
    std::memcpy(&frame[6], &data_len, 2);

    // Payload
    std::memcpy(&frame[8], data.data(), data.size());

    return frame;
}

int main() {
    try {
        io_context io_context;
        tcp::socket socket(io_context);
        tcp::resolver resolver(io_context);
        
        std::cout << "=== Game Client Test ===" << std::endl;
        std::cout << "Connecting to game server..." << std::endl;
        
        // 서버에 연결
        auto endpoints = resolver.resolve("127.0.0.1", "12345");
        connect(socket, endpoints);
        
        std::cout << "Connected to game server!" << std::endl;
        
        // 1. PING 메시지 전송
        std::string ping_data = "PING";
        auto ping_frame = create_frame(GameMessageID::PING, ping_data);
        asio::write(socket, asio::buffer(ping_frame));
        std::cout << "Sent PING message" << std::endl;
        
        // 2. LOGIN 메시지 전송
        std::string login_data = "TestPlayer";
        auto login_frame = create_frame(GameMessageID::LOGIN, login_data);
        asio::write(socket, asio::buffer(login_frame));
        std::cout << "Sent LOGIN message: " << login_data << std::endl;
        
        // 3. MOVE 메시지 전송
        struct MoveData { float x, y, z; } move_coords = {10.0f, 20.0f, 30.0f};
        std::string move_str(reinterpret_cast<char*>(&move_coords), sizeof(MoveData));
        auto move_frame = create_frame(GameMessageID::MOVE, move_str);
        asio::write(socket, asio::buffer(move_frame));
        std::cout << "Sent MOVE message: (" << move_coords.x << ", " << move_coords.y << ", " << move_coords.z << ")" << std::endl;
        
        // 4. CHAT 메시지 전송
        std::string chat_message = "Hello, server!";
        auto chat_frame = create_frame(GameMessageID::CHAT, chat_message);
        asio::write(socket, asio::buffer(chat_frame));
        std::cout << "Sent CHAT message: " << chat_message << std::endl;
        
        // 서버 응답 수신
        std::vector<char> response(1024);
        asio::error_code ec;
        size_t bytes_received = socket.read_some(asio::buffer(response), ec);
        
        if (!ec) {
            std::cout << "Received " << bytes_received << " bytes from server." << std::endl;
            if (bytes_received >= 8) {
                uint32_t received_crc;
                uint16_t received_msg_id;
                uint16_t received_len;
                
                std::memcpy(&received_crc, &response[0], 4);
                std::memcpy(&received_msg_id, &response[4], 2);
                std::memcpy(&received_len, &response[6], 2);
                
                std::string response_data(response.begin() + 8, response.begin() + bytes_received);
                
                std::cout << "Response - CRC: " << received_crc 
                         << ", MSG_ID: " << received_msg_id
                         << ", LEN: " << received_len
                         << ", DATA: " << response_data << std::endl;
            }
        } else if (ec == asio::error::eof) {
            std::cout << "Server closed connection." << std::endl;
        } else {
            std::cout << "Error receiving response: " << ec.message() << std::endl;
        }
        
        socket.close();
        std::cout << "Disconnected from server." << std::endl;
        
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
