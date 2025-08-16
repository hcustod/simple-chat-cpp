#include <iostream>
#include <thread>
#include <string>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <atomic>

#include "commands.h"

const int PORT = 5000;
const char* SERVER_IP = "127.0.0.1";
static std::atomic<bool> running(true);
static int g_sock = -1;

// Helper functions
static bool send_all(int sock, const char* data, size_t len) {
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t n = send(sock, data + total_sent, len - total_sent, 0);
        if (n > 0) {
            total_sent += static_cast<size_t>(n);
            continue;
        }
        if (n == -1 && (errno == EINTR)) {
            continue; // interrupted, retry
        }
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue; // would block; retry on blocking sockets
        }
        return false; // other error or connection closed
    }
    return true;
}

static void trim(std::string& str) {
    const char* ws = " \t\r\n";
    auto start = str.find_first_not_of(ws);
    if (start == std::string::npos) { str.clear(); return; }   // all whitespace
    auto end = str.find_last_not_of(ws);
    str.erase(end + 1);
    str.erase(0, start);
}

void sigint_handler(int) {
    running = false;
    if (g_sock != -1) {
        shutdown(g_sock, SHUT_RDWR);
    }
}

bool is_valid_username(const std::string& username) {
    constexpr std::size_t MAX_USERNAME_LENGTH = 30;
    if (username.empty() || username.length() > MAX_USERNAME_LENGTH) {
        return false; // Empty or too long
    }
    return true;
}

// Reveive loop to handle incoming messages
void receive_loop(int sock) {
    char buffer[ChatCommands::MAX_MESSAGE_LENGTH + 1];
    while (true) {
        ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            std::cout << "Server disconnected.\n";
            running = false;
            break;
        }
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
    std::signal(SIGPIPE, SIG_IGN); // Ignore broken pipe signals

    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr); // Disable cin synchronization with stdio

    std::string username;
    std::cout << "Enter your username: ";
    std::getline(std::cin, username);

    trim(username); // Remove leading/trailing whitespace
    if (username.empty()) {
        std::cerr << "Username cannot be empty.\n";
        g_sock = -1; // Reset global socket
        return 1;
    }

    // Match server side username validation
    if (!ChatCommands::is_valid_username(username)) {
        std::cerr << "Invalid username. Must be 1-100 characters, alphanumeric or underscores only.\n";
        g_sock = -1; // Reset global socket
        return 1;
    }

    // Create socket and connect to server
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket: " << std::strerror(errno) << "\n";
        g_sock = -1; // Reset global socket
        return 1;
    }

    g_sock = sock; // Store global socket for signal handling
    std::signal(SIGINT, sigint_handler); // Handle Ctrl+C gracefully


    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    int ip_ok = inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
    if (ip_ok == 0) {
        std::cerr << "Invalid server IP address format: " << SERVER_IP << "\n";
        close(sock);
        g_sock = -1; // Reset global socket
        return 1;
    } else if (ip_ok == -1) {
        std::cerr << "inet_pton failed: " << std::strerror(errno) << "\n";
        close(sock);
        g_sock = -1; // Reset global socket
        return 1;
    }

    std::cout << "Connecting to server at... " << SERVER_IP << ":" << PORT << "...\n" << std::flush;
    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) { 
        std::cerr << "Failed to connect to server: " << std::strerror(errno) << "\n";
        close(sock);
        g_sock = -1; // Reset global socket
        return 1;
    }

    // Send username first as server expects it
    if (!send_all(sock, username.c_str(), username.length())) {
        std::cerr << "Failed to send username: " << std::strerror(errno) << "\n";
        close(sock);
        g_sock = -1; // Reset global socket
        return 1;
    }

    std::thread rx(receive_loop, sock);

    std::string input;
    std::cout << "> " << std::flush;
    while (running && std::getline(std::cin, input)) {
        trim(input); // Remove leading/trailing whitespace
        if (input.empty()) {
            std::cout << "> " << std::flush;
            continue; // Ignore empty input
        }

        if (input.size() > ChatCommands::MAX_MESSAGE_LENGTH) {
            std::cerr << "Message too long. Max length is " << ChatCommands::MAX_MESSAGE_LENGTH << " characters.\n";
            std::cout << "> " << std::flush;
            continue;
        }

        if (input[0] == '/') {
            auto result = handle_command(input, sock);
            if (result == ChatCommands::CommandResult::Quit) {
                running = false;
                shutdown(sock, SHUT_RDWR);
                break;
            } else if (result == ChatCommands::CommandResult::Invalid) {
                std::cout << "> " << std::flush;
                continue;
            }
            std::cout << "> " << std::flush;
            continue;
        }

        if (!send_all(sock, input.c_str(), input.size())) {
            std::cerr << "Failed to send message: " << std::strerror(errno) << "\n";
            break;
        }
        std::cout << "\r" << input << "\n> " << std::flush;
        input.clear();
    }


    shutdown(sock, SHUT_RDWR);
    if (rx.joinable()) rx.join();
    close(sock);
    g_sock = -1; // Reset global socket
    return 0;
}
