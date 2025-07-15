#include <iostream>
#include <thread>
#include <string>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sstream>

#include "commands.h"

const int PORT = 5000;
const char* SERVER_IP = "127.0.0.1";


// Helpers

bool is_valid_username(const std::string& username) {
    constexpr size_t MAX_LENGTH = 20;
    if (username.empty() || username.length() > MAX_LENGTH) {
        std::cerr << "Username must be between 1 and " << MAX_LENGTH << " characters.\n";
        return false;
    }
}


// Reveive loop to handle incoming messages

void receive_loop(int sock) {
    char buffer[1024];
    while (true) {
        ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        std::cout << "\r" << buffer << "\n> " << std::flush;
    }
}

ChatCommands::CommandResult handle_command(const std::string& input, int sock) {
    std::istringstream iss(input);
    std::string cmd;
    iss >> cmd;

    auto it = ChatCommands::unified_command_table.find(cmd);
    if (it != ChatCommands::unified_command_table.end() && it->second.clientHandler) {
        return it->second.clientHandler(iss, sock); // calls command handler
    } else {
        std::cerr << "Unknown command: " << cmd << "\n";
        return ChatCommands::CommandResult::Invalid;
    }
}

int main() {
    std::string username;
    std::cout << "Enter your username: ";
    std::getline(std::cin, username);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    

    if (username.empty() || username.length() > 100) {
        std::cerr << "Username cannot be empty and must be less than 100 characters.\n";
        return 1;
    }

    if (sock == -1) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }

    std::cout << "Connecting to server at... " << SERVER_IP << ":" << PORT << "...\n" << std::flush;

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) { 
        std::cerr << "Failed to connect to server.\n";
        return 1;
    }

    if (send(sock, username.c_str(), username.length(), 0) == -1) {
        std::cerr << "Failed to send username.\n";
        close(sock);
        return 1;
    }

    std::thread(receive_loop, sock).detach();

    std::string input;
    std::cout << "> ";
    while (getline(std::cin, input)) {
        if (input.length() > 1000) {
            std::cerr << "Message too long. Please limit to 1000 characters.\n";
            std::cout << "> ";
            continue;
        } 

        if (input.rfind('/', 0) == 0) {
            auto result = handle_command(input, sock);
            if (result == ChatCommands::CommandResult::Quit) {
                break;
            } else if (result == ChatCommands::CommandResult::Invalid) {
                std::cout << "> ";
                continue;
            }
        }

        if (send(sock, input.c_str(), input.length(), 0) == -1) {
            std::cerr << "Failed to send message.\n";
            break;
        }
        std::cout << "\r" << input << "\n> " << std::flush;
        input.clear();
        std::cout << "> ";
    }

    close(sock);
    return 0;
}
