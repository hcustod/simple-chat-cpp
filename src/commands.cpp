#include "commands.h"
#include <iostream>
#include <string>
#include <unistd.h>
#include <netinet/in.h>
#include <algorithm>
#include <chrono>

// TODO: 
// Redundancy here in having two lookup tables, one for client commands and one for server commands.

namespace ChatCommands {

namespace Config {
    constexpr size_t MAX_MESSAGE_LENGTH = 1024;
    constexpr size_t MAX_USERNAME_LENGTH = 32;
    constexpr int PING_COOLDOWN_SECONDS = 5;
}

bool is_valid_username(const std::string& name) {
    if (name.empty() || name.length() > Config::MAX_USERNAME_LENGTH) {
        return false;
    }
    return name.find_first_of(" \t\n\r") == std::string::npos;
}
 
bool send_safe(int sock, const std::string& msg) {
    if (msg.length() > Config::MAX_MESSAGE_LENGTH) {
        std::cerr << "Message too long.\n";
        return false;
    }
    if (send(sock, msg.c_str(), msg.length(), 0) == -1) {
        perror("send failed");
        return false;
    }
    return true;
}

const std::string help_text =
    "Available commands:\n"
    "  /quit                 - Exit chat\n"
    "  /help                 - Show this help message\n"
    "  /who                  - List connected users\n"
    "  /whisper <user> <msg> - Private message\n"
    "  /name <new_username>  - Change your username\n"
    "  /clear                - Clear the terminal\n"
    "  /ping                 - Check connection with server\n";

// Track last ping time
std::chrono::steady_clock::time_point last_ping_time =
    std::chrono::steady_clock::now() - std::chrono::seconds(Config::PING_COOLDOWN_SECONDS);

// Global command table
std::unordered_map<std::string, CommandHandler> command_table = {
    {"/quit", [](std::istringstream&, int) {
        std::cout << "Exiting chat...\n";
        return true;
    }},
    {"/help", [](std::istringstream&, int) {
        std::cout << help_text;
        return false;
    }},
    {"/who", [](std::istringstream&, int sock) {
        return send_safe(sock, "/who");
    }},
    {"/whisper", [](std::istringstream& iss, int sock) {
        std::string user;
        iss >> user;
        std::string message;
        std::getline(iss, message);
        message.erase(0, message.find_first_not_of(" \t"));  // trim left

        if (user.empty() || message.empty()) {
            std::cerr << "Usage: /whisper <user> <message>\n";
            return false;
        }

        std::string full = "/whisper " + user + message;
        return send_safe(sock, full);
    }},
    {"/name", [](std::istringstream& iss, int sock) {
        std::string new_name;
        iss >> new_name;

        if (!is_valid_username(new_name)) {
            std::cerr << "Invalid username. It must be 1-" << Config::MAX_USERNAME_LENGTH
                      << " characters with no whitespace.\n";
            return false;
        }

        std::string msg = "/name " + new_name;
        return send_safe(sock, msg);
    }},
    {"/clear", [](std::istringstream&, int) {
        std::cout << "\033[2J\033[1;1H";  // ANSI escape (clear screen)
        return false;
    }},
    {"/ping", [](std::istringstream&, int sock) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_ping_time);

        if (elapsed.count() < Config::PING_COOLDOWN_SECONDS) {
            std::cerr << "Ping rate limit: wait "
                      << (Config::PING_COOLDOWN_SECONDS - elapsed.count())
                      << " more seconds.\n";
            return false;
        }

        last_ping_time = now;
        return send_safe(sock, "/ping");
    }},
};


using ServerCommandHandler = std::function<void(
    int client_fd, 
    const std::string& raw,
    std::unordered_map<int, std::string>& client_names, 
    std::vector<int>& clients, 
    std::mutex& m
)>;

std::unordered_map<std::string, ServerCommandHandler> server_command_table = {
    {"/ping", [](int client_fd, const std::string&, auto&, auto&, auto&) {
        std::string pong = "Server: pong\n";
        send(client_fd, pong.c_str(), pong.length(), 0);
    }},
    {"/help", [](int client_fd, const std::string&, auto&, auto&, auto&) {
        send_safe(client_fd, help_text);
    }},
    {"/who", [](int client_fd, const std::string&, auto& client_names, auto&, std::mutex& m) {
        std::lock_guard<std::mutex> lock(m);
        std::string list = "Connected users:\n";
        for (const auto& [fd, name] : client_names) {
            list += "  " + name + "\n";
        }
        send_safe(client_fd, list);
    }},
    {"/name", [](int client_fd, const std::string& raw, auto& client_names, auto&, std::mutex& m) {
        std::istringstream iss(raw);
        std::string cmd, new_name;
        iss >> cmd >> new_name;

        if (!is_valid_username(new_name)) {
            std::string err = "Invalid username.\n";
            send_safe(client_fd, err);
            return;
        }

        std::lock_guard<std::mutex> lock(m);
        std::string old_name = client_names[client_fd];
        client_names[client_fd] = new_name;

        std::string notice = old_name + " changed name to " + new_name + "\n";
        for (const auto& [fd, _] : client_names) {
            if (fd != client_fd) send_safe(fd, notice);
        }
    }},
    {"/whisper", [](int client_fd, const std::string& raw, auto& client_names, auto&, std::mutex& m) {
        std::istringstream iss(raw);
        std::string cmd, target;
        iss >> cmd >> target;
        std::string msg;
        std::getline(iss, msg);
        msg.erase(0, msg.find_first_not_of(" \t"));

        std::lock_guard<std::mutex> lock(m);
        auto it = std::find_if(client_names.begin(), client_names.end(),
            [&](const auto& p) { return p.second == target; });

        if (it != client_names.end()) {
            std::string reply = "(whisper from " + client_names[client_fd] + "): " + msg + "\n";
            send_safe(it->first, reply);
        } else {
            send_safe(client_fd, "User not found.\n");
        }
    }}
};
}

