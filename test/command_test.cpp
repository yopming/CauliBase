#include <doctest/doctest.h>
#include <sstream>
#include <string>
#include <iostream>

#include "command.h"

TEST_CASE("command.h / parseCommand returns correct enum for all valid commands") {
  CHECK(parseCommand("put") == Command::PUT);
  CHECK(parseCommand("get") == Command::GET);
  CHECK(parseCommand("del") == Command::DEL);
  CHECK(parseCommand("flush") == Command::FLUSH);
  CHECK(parseCommand("compact") == Command::COMPACT);
  CHECK(parseCommand("debug") == Command::DEBUG);
  CHECK(parseCommand("help") == Command::HELP);
  CHECK(parseCommand("exit") == Command::EXIT);
}

TEST_CASE("command.h / parseCommand returns UNKNOWN for invalid commands") {
  CHECK(parseCommand("unknown") == Command::UNKNOWN);
  CHECK(parseCommand("") == Command::UNKNOWN);
  CHECK(parseCommand("PUT") == Command::UNKNOWN);  // case sensitive
  CHECK(parseCommand(" put") == Command::UNKNOWN); // spaces before command
  CHECK(parseCommand("put ") == Command::UNKNOWN); // spaces after command
  CHECK(parseCommand("delete") == Command::UNKNOWN);
}

TEST_CASE("command.h / helpDict contains all expected help strings") {
  CHECK(helpDict.at("put") == "put <key> <value>");
  CHECK(helpDict.at("get") == "get <key>");
  CHECK(helpDict.at("del") == "del <key>");
  CHECK(helpDict.at("flush") == "flush");
  CHECK(helpDict.at("compact") == "compact");
  CHECK(helpDict.at("debug") == "debug");
  CHECK(helpDict.at("help") == "help");
  CHECK(helpDict.at("exit") == "exit");
}

TEST_CASE("command.h / commands map contains all expected command mappings") {
  CHECK(commands.at("put") == Command::PUT);
  CHECK(commands.at("get") == Command::GET);
  CHECK(commands.at("del") == Command::DEL);
  CHECK(commands.at("flush") == Command::FLUSH);
  CHECK(commands.at("compact") == Command::COMPACT);
  CHECK(commands.at("debug") == Command::DEBUG);
  CHECK(commands.at("help") == Command::HELP);
  CHECK(commands.at("exit") == Command::EXIT);
}

TEST_CASE("command.h / printHelp prints header and all command descriptions") {
  std::ostringstream oss;
  std::streambuf *oldBuf = std::cout.rdbuf(oss.rdbuf());

  printHelp();

  std::cout.rdbuf(oldBuf);

  std::string output = oss.str();

  CHECK(output.find("Commands:") != std::string::npos);
  CHECK(output.find("put <key> <value>") != std::string::npos);
  CHECK(output.find("get <key>") != std::string::npos);
  CHECK(output.find("del <key>") != std::string::npos);
  CHECK(output.find("flush") != std::string::npos);
  CHECK(output.find("compact") != std::string::npos);
  CHECK(output.find("debug") != std::string::npos);
  CHECK(output.find("help") != std::string::npos);
  CHECK(output.find("exit") != std::string::npos);
}
