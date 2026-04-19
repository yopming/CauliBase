#include <exception>
#include <iostream>
#include <sstream>
#include <string>

#include "cauli_base.h"
#include "command.h"
#include "main.h"

void handlePut(CauliBase &db, std::istringstream &iss) {
  std::string key, val;
  iss >> key;             // get 'key' first
  std::getline(iss, val); // get rest of the line as 'val'

  // key is empty
  if (key.empty()) {
    std::cout << "Usage: " << helpDict.at("put") << "\n";
    return;
  }

  // trim leading spaces
  if (!val.empty() && val.front() == ' ') {
    val.erase(val.begin());
  }

  db.put(key, val);
  std::cout << "OK \n";
}

void handleGet(CauliBase &db, std::istringstream &iss) {
  std::string key;
  iss >> key; // get 'key'

  if (key.empty()) {
    std::cout << "Usage: " << helpDict.at("get") << "\n";
    return;
  }

  auto val = db.get(key);
  if (val.has_value()) {
    std::cout << *val << "\n";
  } else {
    std::cout << "(value not found for key " << key << ") \n";
  }
}

void handleDel(CauliBase &db, std::istringstream &iss) {
  std::string key;
  iss >> key; // get 'key'

  if (key.empty()) {
    std::cout << "Usage: " << helpDict.at("del") << "\n";
    return;
  }

  db.del(key);
  std::cout << "OK \n";
}

void handleFlush(CauliBase &db) {
  db.flush();
  std::cout << "Flushed \n";
}

void handleCompact(CauliBase &db) {
  db.compact();
  std::cout << "Compacted \n";
}

void handleDebug(CauliBase &db) { db.printDebug(); }

void handleHelp() { printHelp(); }

/**
 * @brief Entry function
 *
 * @return int
 */
#ifndef CAULI_BASE_DISABLE_MAIN
int main() {
  try {
    CauliBase db("data", 4); // initialize database instance

    std::cout << "CauliBase: \n";
    printHelp();

    std::string line; // buffer for user input

    while (true) {
      std::cout << ">  ";
      if (!std::getline(std::cin, line)) {
        break;
      }

      std::istringstream iss(line); // wrap std::string to string stream
      std::string cmd;              // var for commands like "put" "del" "get"
      iss >> cmd;                   // first word as command

      Command c = parseCommand(cmd);
      switch (c) {
      case Command::PUT:
        handlePut(db, iss);
        break;
      case Command::GET:
        handleGet(db, iss);
        break;
      case Command::DEL:
        handleDel(db, iss);
        break;
      case Command::FLUSH:
        handleFlush(db);
        break;
      case Command::COMPACT:
        handleCompact(db);
        break;
      case Command::DEBUG:
        handleDebug(db);
        break;
      case Command::HELP:
        handleHelp();
        break;
      case Command::EXIT:
        return 0;
      case Command::UNKNOWN:
        std::cout << "Unknown command \n";
        break;
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
#endif
