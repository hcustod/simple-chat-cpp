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
#include <iomanip>
#include <sstream>
#include <cerrno>
#include <cctype>
#include <sys/socket.h>

#include "commands.h"

namespace { 
    std::mutex send_m;
    std::unordered_map<int, int> send_failures; // fd -> number of consecutive send failures
    inline bool should_drop_fd(int fd, int threshold = 3) {
        std::lock_guard<std::mutex> lock(send_m);
        auto it = send_failures.find(fd);
        return it != send_failures.end() && it->second >= threshold;
        
    }

    inline void clear_fd_failures(int fd) {
        std::lock_guard<std::mutex> lock(send_m);
        send_failures.erase(fd);
    }
}


// To be included in silent messages 
constexpr int WINDOW_SECONDS = 5;          // Sliding window length
constexpr int MAX_MSGS_PER_WINDOW = 15;    // Max messages allowed per window
constexpr int MUTE_SECONDS = 10;           // Temporary mute duration

const int PORT = 5000;

std::vector<int> clients;
std::mutex m;
std::unordered_map<int, std::string> client_names;

volatile std::sig_atomic_t stop_server = false;
volatile std::sig_atomic_t last_signal = 0;

void signal_handler(int signal) {
    last_signal = signal;
    if (signal == SIGINT || signal == SIGTERM) {
        stop_server = true;
    }
}

void print_signal_message(int signal) {
    switch (signal) {
        case SIGINT:
            std::cout << "Received SIGINT (Ctrl+C). Stopping server...\n";
            break;
        case SIGTERM:
            std::cout << "Received SIGTERM. Stopping server...\n";
            break;
        default:
            std::cout << "Received signal " << signal << ". Stopping server...\n";
            break;
    }
}

std::string sanitize_input(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) {
        return !std::isprint(c) || c == '\n' || c == '\r' || c == '\t';
    }), s.end());
    return s;
}


static std::string get_time() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm); // Thread-safe version of localtime
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

inline void record_send_failure(int fd) {
    std::lock_guard<std::mutex> lock(send_m);
    ++send_failures[fd];
}

void broadcast(const std::string& message, int sender_fd) {
    std::vector<int> snapshot; 
    {
        std::lock_guard<std::mutex> lock(m);
        snapshot = clients; // Take a snapshot of the current clients
    }

    std::vector<int> to_remove;
    for (int client_fd : snapshot) {
        if (client_fd == sender_fd) {
            continue; // Skip sending to the sender or clients that should be dropped
        }
        if (ChatCommands::send_safe(client_fd, message)) {
            clear_fd_failures(client_fd);
         } else {
            record_send_failure(client_fd); // Record the failure
            if (should_drop_fd(client_fd)) {
                std::cout << "Dropping client " << client_fd << " due to consecutive send failures.\n";
                to_remove.push_back(client_fd); // Mark for removal
            }
        }
    }

    if (!to_remove.empty()) {
        std::vector<int> to_shutdown;
        {
            std::lock_guard<std::mutex> lock(m);
            for (int fd : to_remove) {
                clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
                client_names.erase(fd); // Remove from client names
                to_shutdown.push_back(fd); // Collect fds to shutdown
            }
        }

        for (int fd : to_shutdown) {
            shutdown(fd, SHUT_RDWR); // Shutdown the client socket
            clear_fd_failures(fd); // Clear failures for this fd
            // Allow handle_client to own the final close
        }
    }
}

void handle_client(int client_fd) {
    char buffer[ChatCommands::MAX_MESSAGE_LENGTH + 1];

    // Get initial username
    ssize_t name_bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (name_bytes <= 0) {
        close(client_fd);
        return;
    }
    buffer[name_bytes] = '\0';
    std::string client_name = sanitize_input(buffer);

    // Validate username
    if (!ChatCommands::is_valid_username(client_name)) {
        std::string error_msg = "Invalid username. Must be alphanumeric, underscore, or hyphen, and not empty or too long.\n";
        ChatCommands::send_safe(client_fd, error_msg);
        close(client_fd);
        return;
    }

    //Check duplicate username
    {
        std::lock_guard<std::mutex> lock(m);
        for (const auto& pair : client_names) {
            if (pair.second == client_name) {
                std::string error_msg = "Username already taken. Please choose another one.\n";
                ChatCommands::send_safe(client_fd, error_msg);
                close(client_fd);
                return;
            }
        }
        client_names[client_fd] = client_name;
        clients.push_back(client_fd); // Add to clients list
    }

    // Announce client joining
    {
        std::string welcome_message = client_name + " has joined the chat.\n";
        ChatCommands::send_safe(client_fd, welcome_message); // Send welcome message to the new client
        broadcast(welcome_message, client_fd);
    }

    // Main loop to handle client messages
    while (true) {
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            std::cout << "Client disconnected: " << client_name << "\n";
            break;
        }

        buffer[bytes] = '\0';
        std::string msg = sanitize_input(buffer);
        if (msg.empty()) continue; // Ignore empty messages
        if (msg.length() > ChatCommands::MAX_MESSAGE_LENGTH) {
            std::string error_msg = "Message too long. Max length is 1024 characters.\n";
            ChatCommands::send_safe(client_fd, error_msg);
            continue; // Skip broadcasting this message
        }        

        // Handle server-side command
        if (msg[0] == '/') {
            std::istringstream iss(msg);
            std::string command;
            iss >> command;

            auto it = ChatCommands::unified_command_table.find(command);
            if (it != ChatCommands::unified_command_table.end() && it->second.serverHandler) {
                // Call the server-side command handler
                it->second.serverHandler(client_fd, msg, client_names, clients, m);
            } else {
                // Unknown command
                std::string error_msg = "Unknown command: " + command + "\n";
                ChatCommands::send_safe(client_fd, error_msg);
            }

            continue;
        }

        // Handle standard message
        std::string name_snapshot;
        {
            std::lock_guard<std::mutex> lock(m);
            auto it = client_names.find(client_fd);
            name_snapshot = (it != client_names.end()) ? it->second : client_name;
        }
        std::string full_msg = get_time() + " " + name_snapshot + ": " + msg + "\n";
        std::cout << full_msg;
        broadcast(full_msg, client_fd);
    }

    // Cleanup after disconnection
    std::string name_snapshot;
    { 
        std::lock_guard<std::mutex> lock(m);
        auto it = client_names.find(client_fd);
        name_snapshot = (it != client_names.end()) ? it->second : client_name;

        clients.erase(std::remove(clients.begin(), clients.end(), client_fd), clients.end());
        client_names.erase(client_fd); // Remove from client names
    }
    {
        clear_fd_failures(client_fd); // Clear failures for this fd
    }

    std::string full_message = get_time() + " " + name_snapshot + " has left the chat.\n";
    std::cout << full_message;
    broadcast(full_message, client_fd);
    close(client_fd); // Close the client connection
}


int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN); // Ignore broken pipe signals

    // Server port/socket creation
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (server_sock < 0) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }

    // Set socket options to reuse address (for quick server restarts)
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int one = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

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
            if (errno == EINTR) {                 // interrupted by signal
                if (stop_server) break;           // asked to stop
                continue;                         // try again
            }
            if (stop_server) break;
            std::cerr << "Failed to accept client connection.\n";
            continue;
        }

        std::thread(handle_client, client_conn).detach();
    }

    std::cout << "Server shutting down...\n";
    if (last_signal) {
        print_signal_message(last_signal);
    }
    close(server_sock);

    // Snapshot under lock
    std::vector<int> fds;
    {
        std::lock_guard<std::mutex> lock(m);
        fds = clients; // Get current client fds
    }

    // Send without blocking
    for (int fd : fds) {
        ChatCommands::send_safe(fd, "Server is shutting down. Goodbye!\n");
        shutdown(fd, SHUT_RDWR); // Shutdown the client socket
    }

    // Prune under lock
    {
        std::lock_guard<std::mutex> lock(m);
        clients.clear(); // Clear the client list
        client_names.clear(); // Clear client names
    }
    {
        std::lock_guard<std::mutex> send_lock(send_m);
        send_failures.clear(); // Clear send failures

    }

    return 0;
}




