#pragma once

#include <sstream>
#include <functional>
#include <unordered_map>
#include <string>

namespace ChatCommands {

    using CommandHandler = std::function<bool(std::istringstream&, int)>;
    extern std::unordered_map<std::string, CommandHandler> command_table;

} 
