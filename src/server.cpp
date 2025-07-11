#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unordered_map>
#include <ctime>
#include <algorithm>

#include "commands.h"

const int PORT = 5000;

std::vector<int> clients;
std::mutex m;
std::unordered_map<int, std::string> client_names;

void broadcast(const std::string& message, int sender_fd) {
    std::lock_guard<std::mutex> lock(m);
    for (int client_fd : clients) {
        if (client_fd != sender_fd) {
            send(client_fd, message.c_str(), message.length(), 0);
        }
    }
}

std::string get_time() {
    time_t now = time(nullptr);
    struct tm* local_time = localtime(&now);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local_time);
    return std::string(buffer);
}

void handle_client(int client_fd) {
    char buffer[1024];

    // Get initial username
    ssize_t name_bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (name_bytes <= 0) {
        close(client_fd);
        return;
    }
    buffer[name_bytes] = '\0';
    std::string client_name(buffer);

    {
        std::lock_guard<std::mutex> lock(m);
        client_names[client_fd] = client_name;
    }

    std::string welcome_message = client_name + " has joined the chat.\n";
    broadcast(welcome_message, client_fd);
    std::cout << welcome_message;

    while (true) {
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            std::cout << "Client disconnected: " << client_name << "\n";
            break;
        }

        buffer[bytes] = '\0';
        std::string msg(buffer);

        // Sanitize message
        msg.erase(std::remove(msg.begin(), msg.end(), '\n'), msg.end()); // Remove newlines
        msg.erase(std::remove(msg.begin(), msg.end(), '\r'), msg.end()); // Remove carriage returns
        msg.erase(std::remove(msg.begin(), msg.end(), '\t'), msg.end()); // Remove tabs
        msg.erase(std::remove_if(msg.begin(), msg.end(), [](unsigned char c) {
            return !std::isprint(c); // Remove non-printable characters
        }), msg.end());
        if (msg.empty()) continue; // Ignore empty messages
        if (msg.length() > 1024) {
            std::string error_msg = "Message too long. Max length is 1024 characters.\n";
            send(client_fd, error_msg.c_str(), error_msg.length(), 0);
            continue; // Skip broadcasting this message
        }

        // Handle server-side command
        if (!msg.empty() && msg[0] == '/') {
            std::istringstream iss(msg);
            std::string command;
            iss >> command;

            auto it = ChatCommands::server_command_table.find(command);
            if (it != ChatCommands::server_command_table.end()) {
                it->second(client_fd, msg, client_names, clients, m);
                continue; // Skip broadcasting this message
            } else {
                std::string error_msg = "Unknown command: " + command + "\n";
                send(client_fd, error_msg.c_str(), error_msg.length(), 0);
                continue;
            }
        }

        // Handle standard message
        std::string full_msg = get_time() + " " + client_names[client_fd] + ": " + msg + "\n";
        std::cout << full_msg;
        broadcast(full_msg, client_fd);
    }

    // Cleanup after disconnection 
    close(client_fd);
    {
        std::lock_guard<std::mutex> lock(m);
        clients.erase(std::remove(clients.begin(), clients.end(), client_fd), clients.end());
        client_names.erase(client_fd);

        std::string leave_message = client_name + " has left the chat.\n";
        std::cout << leave_message;
        broadcast(leave_message, client_fd);
    }
}



int main() {
    // Server port
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (server_sock < 0) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }

    // Set socket options to reuse address (for quick server restarts)
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Set server addr
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    // Attach server to IP:Port and wait for incoming conns
    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind socket.\n";
        close(server_sock);
        return 1;
    }
    if (listen(server_sock, 10) < 0) {
        std::cerr << "Failed to listen on socket.\n";
        close(server_sock);
        return 1;
    }

    std::cout << "Server listening on port... " << PORT << std::endl;

    // Accept clients in a loop
    while (true) {
        int client_conn = accept(server_sock, nullptr, nullptr);

        if (client_conn < 0) {
            std::cerr << "Failed to accept client connection.\n";
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m);
            clients.push_back(client_conn);
        }

        std::thread(handle_client, client_conn).detach();
    }

    close(server_sock);
    return 0;
}




