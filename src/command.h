#pragma once

#include <iostream>
#include <string>
#include <unordered_map>

enum class Command { PUT, GET, DEL, FLUSH, COMPACT, DEBUG, HELP, EXIT, UNKNOWN };

static const std::unordered_map<std::string, std::string> helpDict = {
    {"put", "put <key> <value>"},
    {"get", "get <key>"},
    {"del", "del <key>"},
    {"flush", "flush"},
    {"compact", "compact"},
    {"debug", "debug"},
    {"help", "help"},
    {"exit", "exit"},
};

static const std::unordered_map<std::string, Command> commands = {
    {"put", Command::PUT},
    {"get", Command::GET},
    {"del", Command::DEL},
    {"flush", Command::FLUSH},
    {"compact", Command::COMPACT},
    {"debug", Command::DEBUG},
    {"help", Command::HELP},
    {"exit", Command::EXIT},
};

/**
 * @brief static function to print help menu
 */
static void printHelp() {
  std::cout << "Commands: \n";
  for (const auto &[cmd, desc] : helpDict) {
    std::cout << "  " << desc << "\n";
  }
}

/**
 * @brief Parse string command to enum 'Command'
 * 
 * @param cmd string user input
 * @return Command 
 */
inline Command parseCommand(const std::string &cmd) {
  if (commands.find(cmd) != commands.end()) {
    return commands.at(cmd);
  } else {
    return Command::UNKNOWN;
  }
}
