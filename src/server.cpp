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
#include <csignal>

#include "commands.h"

const int PORT = 5000;
constexpr int MAX_MESSAGE_LENGTH = 1024;

std::vector<int> clients;
std::mutex m;
std::unordered_map<int, std::string> client_names;

volatile std::sig_atomic_t stop_server = false;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "Received signal to stop server.\n";
        stop_server = true;
    }
}

std::string get_time() {
    time_t now = time(nullptr);
    struct tm* local_time = localtime(&now);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local_time);
    return std::string(buffer);
}

// Proper way to track send failures
std::unordered_map<int, int> send_failures;

bool track_send_fails(int fd) {
    std::lock_guard<std::mutex> lock(m);
    send_failures[fd]++;

    if (send_failures[fd] >= 3) {
        std::cerr << "Too many send failures for client: " << fd 
                  << ". Disconnecting client.\n";
        send_failures.erase(fd); // Reset failure count for this client
        close(fd);
        return true; // Disconnect the client after too many failures
    }
    return false; // Continue trying to send data
}


void reset_send_fail_count(int fd) {
    std::lock_guard<std::mutex> lock(m);
    send_failures.erase(fd); // Reset failure count for this client
}

bool safe_send(int fd, std::string_view data) {
    ssize_t bytes_sent = send(fd, data.data(), data.size(), 0);
    if (bytes_sent < 0) {
        std::cerr << "Failed to send data to client: " << fd << "\n";
        return track_send_fails(fd);
    }
    reset_send_fail_count(fd);
    return true; // Successfully sent data
 
}

void broadcast(const std::string& message, int sender_fd) {
    std::lock_guard<std::mutex> lock(m);
    for (int client_fd : clients) {
        if (client_fd != sender_fd) {
            safe_send(client_fd, message);
        }
    }
}

void handle_client(int client_fd) {
    char buffer[MAX_MESSAGE_LENGTH + 1];

    // Get initial username
    ssize_t name_bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (name_bytes <= 0) {
        close(client_fd);
        return;
    }
    buffer[name_bytes] = '\0';
    std::string client_name(buffer);

    // TODO: Fix for checking duplicate usernames ****
    std::lock_guard<std::mutex> lock(m);
    for (const auto& [fd, name] : client_names) {
        if (name == client_name) {
            safe_send(client_fd, "Username already taken. Disconnecting.\n");
            close(client_fd);
            return;
        }
    }
    client_names[client_fd] = client_name;

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
        if (msg.length() > MAX_MESSAGE_LENGTH) {
            std::string error_msg = "Message too long. Max length is 1024 characters.\n";
            safe_send(client_fd, error_msg);
            continue; // Skip broadcasting this message
        }

        // Handle server-side command
        if (!msg.empty() && msg[0] == '/') {
            std::istringstream iss(msg);
            std::string command;
            iss >> command;

            auto it = ChatCommands::unified_command_table.find(command);
            if (it != ChatCommands::unified_command_table.end() && it->second.serverHandler) {
                // Call the server-side command handler
                it->second.serverHandler(client_fd, msg, client_names, clients, m);
                continue; // Skip broadcasting this message
            } else {
                // Unknown command
                std::string error_msg = "Unknown command: " + command + "\n";
                safe_send(client_fd, error_msg);
                continue; // Skip broadcasting this message

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
        send_failures.erase(client_fd); // Remove from send failures
        clients.erase(std::remove(clients.begin(), clients.end(), client_fd), clients.end());
        client_names.erase(client_fd);

        std::string leave_message = client_name + " has left the chat.\n";
        std::cout << leave_message;
        broadcast(leave_message, client_fd);
    }
}

// Function to get the number of connected client -- TODO: Where to implement this?
int get_connected_client_count() {
    std::lock_guard<std::mutex> lock(m);
    return clients.size();
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Server port/socket creation
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
    while (!stop_server) {
        int client_conn = accept(server_sock, nullptr, nullptr);

        if (client_conn < 0) {
            if (stop_server) {
                break; // Exit loop if server is stopping
            }
            std::cerr << "Failed to accept client connection.\n";
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m);
            clients.push_back(client_conn);
        }

        std::thread(handle_client, client_conn).detach();
    }

    std::cout << "Server shutting down...\n";
    close(server_sock);

    {
        std::lock_guard<std::mutex> lock(m);
        for (int client_fd : clients) {
            safe_send(client_fd, "Server is shutting down. Goodbye!\n");
            shutdown(client_fd, SHUT_RDWR); // Shutdown the client socket
            close(client_fd); // Close the client connection
        }
        clients.clear();
        client_names.clear();
        send_failures.clear();
    }

    return 0;
}




