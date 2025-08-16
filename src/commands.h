#pragma once

#include <sstream>
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

namespace ChatCommands {

    // Constants (header-only, safe as constexprs)
    constexpr std::size_t MAX_MESSAGE_LENGTH   = 1024;
    constexpr std::size_t MAX_USERNAME_LENGTH  = 32;
    constexpr int         PING_COOLDOWN_SECONDS = 5;

    // Result of client-side command processing
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

    struct UnifiedCommand {
        ClientCommandHandler clientHandler;
        ServerCommandHandler serverHandler;
    };

    // ---- Declarations (definitions live in commands.cpp) ----
    bool is_valid_username(const std::string& name);
    bool send_safe(int sock, const std::string& msg);

    extern const std::string help_text;
    extern std::chrono::steady_clock::time_point last_ping_time;
    extern std::unordered_map<std::string, UnifiedCommand> unified_command_table;

} // namespace ChatCommands
