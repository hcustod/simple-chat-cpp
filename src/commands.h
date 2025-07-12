#pragma once

#include <sstream>
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>

namespace ChatCommands {

    constexpr std::size_t MAX_MESSAGE_LENGTH = 1024;
    constexpr std::size_t MAX_USERNAME_LENGTH = 32;
    constexpr int PING_COOLDOWN_SECONDS = 5;

    enum class CommandResult {
        Continue,
        Quit,
        Invalid
    };

    using ClientCommandHandler = std::function<CommandResult(std::istringstream&, int)>;

    using ServerCommandHandler = std::function<void(
        int client_fd,
        const std::string& raw,
        std::unordered_map<int, std::string>& client_names,
        std::vector<int>& clients,
        std::mutex& m
    )>;

    using UnifiedCommand = struct {
        ClientCommandHandler clientHandler;
        ServerCommandHandler serverHandler;
    };

    extern std::unordered_map<std::string, UnifiedCommand> unified_command_table;

    bool is_valid_username(const std::string& name);
    bool send_safe(int sock, const std::string& msg);
} 
