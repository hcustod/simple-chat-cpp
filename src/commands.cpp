#include "commands.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

namespace ChatCommands {

// ---------- Function definitions declared in commands.h ----------

bool is_valid_username(const std::string& s) {
    if (s.empty() || s.size() > MAX_USERNAME_LENGTH) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

// Robust "send all" that respects MAX_MESSAGE_LENGTH and retries on partial sends.
bool send_safe(int sock, const std::string& msg) {
    if (msg.size() > MAX_MESSAGE_LENGTH) {
        std::cerr << "Message too long.\n";
        return false;
    }
    const char* buf = msg.data();
    size_t left = msg.size();
    while (left > 0) {
        ssize_t n = ::send(sock, buf, left, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) { continue; } // could add small sleep/poll
            perror("send failed");
            return false;
        }
        buf  += n;
        left -= static_cast<size_t>(n);
    }
    return true;
}

// ---------- Extern objects from commands.h ----------

const std::string help_text =
    "Available commands:\n"
    "  /quit                 - Exit chat\n"
    "  /help                 - Show this help message\n"
    "  /who                  - List connected users\n"
    "  /whisper <user> <msg> - Private message\n"
    "  /name <new_username>  - Change your username\n"
    "  /clear                - Clear the terminal\n"
    "  /ping                 - Check connection with server\n";

std::chrono::steady_clock::time_point last_ping_time =
    std::chrono::steady_clock::now() - std::chrono::seconds(PING_COOLDOWN_SECONDS);

// Helper to trim leading spaces/tabs from a string (used for messages after command tokens)
static inline void ltrim_inplace(std::string& s) {
    auto pos = s.find_first_not_of(" \t");
    if (pos != std::string::npos) s.erase(0, pos);
    else s.clear();
}

std::unordered_map<std::string, UnifiedCommand> unified_command_table = {
    {
        "/quit",
        {
            // Client
            [](std::istringstream&, int) {
                std::cout << "Exiting chat...\n";
                return CommandResult::Quit;
            },
            // Server
            nullptr
        }
    },
    {
        "/help",
        {
            // Client
            [](std::istringstream&, int) {
                std::cout << help_text;
                return CommandResult::Continue;
            },
            // Server
            [](int client_fd, const std::string&, std::unordered_map<int, std::string>&, std::vector<int>&, std::mutex&) {
                send_safe(client_fd, help_text);
            }
        }
    },
    {
        "/who",
        {
            // Client
            [](std::istringstream&, int sock) {
                return send_safe(sock, "/who\n") ? CommandResult::Continue : CommandResult::Invalid;
            },
            // Server
            [](int client_fd, const std::string&, std::unordered_map<int, std::string>& client_names, std::vector<int>&, std::mutex& m) {
                std::lock_guard<std::mutex> lock(m);
                std::string list = "Connected users:\n";
                for (const auto& [fd, name] : client_names) {
                    (void)fd;
                    list += "  " + name + "\n";
                }
                send_safe(client_fd, list);
            }
        }
    },
    {
        "/whisper",
        {
            // Client
            [](std::istringstream& iss, int sock) {
                std::string user; iss >> user;
                std::string message; std::getline(iss, message);
                ltrim_inplace(message);

                if (user.empty() || message.empty()) {
                    std::cerr << "Usage: /whisper <user> <message>\n";
                    return CommandResult::Invalid;
                }

                std::string full = "/whisper " + user + " " + message + "\n";
                return send_safe(sock, full) ? CommandResult::Continue : CommandResult::Invalid;
            },
            // Server
            [](int client_fd, const std::string& raw, std::unordered_map<int, std::string>& client_names, std::vector<int>&, std::mutex& m) {
                std::istringstream iss(raw);
                std::string cmd, target; iss >> cmd >> target;
                std::string msg; std::getline(iss, msg);
                ltrim_inplace(msg);

                std::lock_guard<std::mutex> lock(m);
                auto it = std::find_if(client_names.begin(), client_names.end(),
                    [&](const auto& p) { return p.second == target; });

                if (it != client_names.end()) {
                    std::string reply = "(whisper from " + client_names[client_fd] + "): " + msg + "\n";
                    send_safe(it->first, reply);
                } else {
                    send_safe(client_fd, "User not found.\n");
                }
            }
        }
    },
    {
        "/name",
        {
            // Client
            [](std::istringstream& iss, int sock) {
                std::string new_name; iss >> new_name;

                if (!is_valid_username(new_name)) {
                    std::cerr << "Invalid username. It must be 1-" << MAX_USERNAME_LENGTH
                              << " characters (letters, digits, '_' or '-').\n";
                    return CommandResult::Invalid;
                }

                return send_safe(sock, std::string("/name ") + new_name) + "\n"
                       ? CommandResult::Continue : CommandResult::Invalid;
            },
            // Server
            [](int client_fd, const std::string& raw, std::unordered_map<int, std::string>& client_names, std::vector<int>&, std::mutex& m) {
                std::istringstream iss(raw);
                std::string cmd, new_name; iss >> cmd >> new_name;

                if (!is_valid_username(new_name)) {
                    send_safe(client_fd, "Invalid username.\n");
                    return;
                }

                std::lock_guard<std::mutex> lock(m);
                std::string old_name = client_names[client_fd];
                client_names[client_fd] = new_name;

                std::string notice = old_name + " changed name to " + new_name + "\n";
                for (const auto& [fd, _] : client_names) {
                    if (fd != client_fd) send_safe(fd, notice);
                }
            }
        }
    },
    {
        "/clear",
        {
            // Client
            [](std::istringstream&, int) {
                std::cout << "\033[2J\033[1;1H";
                return CommandResult::Continue;
            },
            // Server
            nullptr
        }
    },
    {
        "/ping",
        {
            // Client
            [](std::istringstream&, int sock) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_ping_time);

                if (elapsed.count() < PING_COOLDOWN_SECONDS) {
                    std::cerr << "Ping rate limit: wait "
                              << (PING_COOLDOWN_SECONDS - elapsed.count())
                              << " more seconds.\n";
                    return CommandResult::Invalid;
                }

                last_ping_time = now;
                return send_safe(sock, "/ping\n") ? CommandResult::Continue : CommandResult::Invalid;
            },
            // Server
            [](int client_fd, const std::string&, std::unordered_map<int, std::string>&, std::vector<int>&, std::mutex&) {
                send_safe(client_fd, "Server: pong\n");
            }
        }
    },
};

} // namespace ChatCommands
