#include <doctest/doctest.h>

#include "cauli_base.h"
#include "main.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace {

class TempDir {
public:
  explicit TempDir(const std::string &name) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("cauli_main_test_" + name + "_" + std::to_string(now));
    std::filesystem::remove_all(path_);
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

class CoutCapture {
public:
  CoutCapture() : old_buffer_(std::cout.rdbuf(output_.rdbuf())) {}

  ~CoutCapture() { std::cout.rdbuf(old_buffer_); }

  std::string str() const { return output_.str(); }

private:
  std::ostringstream output_;
  std::streambuf *old_buffer_;
};

} // namespace

TEST_CASE("main.cpp / handlePut stores key and value and prints OK") {
  TempDir temp("put");
  CauliBase db(temp.path(), 10);
  std::istringstream input("name Alice");

  CoutCapture output;
  handlePut(db, input);

  CHECK(output.str() == "OK \n");
  REQUIRE(db.get("name").has_value());
  CHECK(*db.get("name") == "Alice");
}

TEST_CASE("main.cpp / handlePut keeps the rest of the line as the value") {
  TempDir temp("put_spaces");
  CauliBase db(temp.path(), 10);
  std::istringstream input("message hello world from CauliBase");

  CoutCapture output;
  handlePut(db, input);

  CHECK(output.str() == "OK \n");
  REQUIRE(db.get("message").has_value());
  CHECK(*db.get("message") == "hello world from CauliBase");
}

TEST_CASE("main.cpp / handlePut accepts an empty value") {
  TempDir temp("put_empty_value");
  CauliBase db(temp.path(), 10);
  std::istringstream input("empty");

  CoutCapture output;
  handlePut(db, input);

  CHECK(output.str() == "OK \n");
  REQUIRE(db.get("empty").has_value());
  CHECK(db.get("empty")->empty());
}

TEST_CASE("main.cpp / handlePut prints usage when key is missing") {
  TempDir temp("put_missing_key");
  CauliBase db(temp.path(), 10);
  std::istringstream input("");

  CoutCapture output;
  handlePut(db, input);

  CHECK(output.str() == "Usage: put <key> <value>\n");
}

TEST_CASE("main.cpp / handleGet prints an existing value") {
  TempDir temp("get_found");
  CauliBase db(temp.path(), 10);
  db.put("name", "Alice");
  std::istringstream input("name");

  CoutCapture output;
  handleGet(db, input);

  CHECK(output.str() == "Alice\n");
}

TEST_CASE("main.cpp / handleGet prints not found for missing values") {
  TempDir temp("get_missing");
  CauliBase db(temp.path(), 10);
  std::istringstream input("missing");

  CoutCapture output;
  handleGet(db, input);

  CHECK(output.str() == "(value not found for key missing) \n");
}

TEST_CASE("main.cpp / handleGet prints usage when key is missing") {
  TempDir temp("get_missing_key");
  CauliBase db(temp.path(), 10);
  std::istringstream input("");

  CoutCapture output;
  handleGet(db, input);

  CHECK(output.str() == "Usage: get <key>\n");
}

TEST_CASE("main.cpp / handleDel deletes a key and prints OK") {
  TempDir temp("del");
  CauliBase db(temp.path(), 10);
  db.put("name", "Alice");
  std::istringstream input("name");

  CoutCapture output;
  handleDel(db, input);

  CHECK(output.str() == "OK \n");
  CHECK(db.get("name").has_value() == false);
}

TEST_CASE("main.cpp / handleDel prints usage when key is missing") {
  TempDir temp("del_missing_key");
  CauliBase db(temp.path(), 10);
  std::istringstream input("");

  CoutCapture output;
  handleDel(db, input);

  CHECK(output.str() == "Usage: del <key>\n");
}

TEST_CASE("main.cpp / handleFlush flushes data and prints Flushed") {
  TempDir temp("flush");
  CauliBase db(temp.path(), 10);
  db.put("name", "Alice");

  CoutCapture output;
  handleFlush(db);

  CHECK(output.str() == "Flushed \n");
  CHECK(std::filesystem::exists(temp.path() / "000001.sst"));
  CHECK(std::filesystem::file_size(temp.path() / "wal.log") == 0);
}

TEST_CASE("main.cpp / handleCompact compacts data and prints Compacted") {
  TempDir temp("compact");
  CauliBase db(temp.path(), 1);
  db.put("name", "Alice");
  db.put("name", "Bob");

  CoutCapture output;
  handleCompact(db);

  CHECK(output.str() == "Compacted \n");
  REQUIRE(db.get("name").has_value());
  CHECK(*db.get("name") == "Bob");
}

TEST_CASE("main.cpp / handleDebug prints database debug info") {
  TempDir temp("debug");
  CauliBase db(temp.path(), 10);
  db.put("name", "Alice");

  CoutCapture output;
  handleDebug(db);

  CHECK(output.str().find("Memtable size: 1") != std::string::npos);
  CHECK(output.str().find("SSTable count: 0") != std::string::npos);
}

TEST_CASE("main.cpp / handleHelp prints command descriptions") {
  CoutCapture output;

  handleHelp();

  CHECK(output.str().find("Commands:") != std::string::npos);
  CHECK(output.str().find("put <key> <value>") != std::string::npos);
  CHECK(output.str().find("get <key>") != std::string::npos);
  CHECK(output.str().find("del <key>") != std::string::npos);
}
