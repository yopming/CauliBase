#include <doctest/doctest.h>

#include "wal.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class TempDir {
public:
  explicit TempDir(const std::string &name) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("cauli_wal_test_" + name + "_" + std::to_string(now));
    std::filesystem::create_directories(path_);
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

void writeBytes(const std::filesystem::path &path, const std::vector<char> &bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  REQUIRE(out.is_open());
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  REQUIRE(out.good());
}

void appendHeader(std::vector<char> &bytes, char op, std::uint32_t key_size, std::uint32_t val_size) {
  bytes.push_back(op);

  const auto *key_ptr = reinterpret_cast<const char *>(&key_size);
  bytes.insert(bytes.end(), key_ptr, key_ptr + sizeof(key_size));

  const auto *val_ptr = reinterpret_cast<const char *>(&val_size);
  bytes.insert(bytes.end(), val_ptr, val_ptr + sizeof(val_size));
}

} // namespace

TEST_CASE("wal.h / WAL creates log file when constructed") {
  TempDir temp("create_file");
  const auto wal_path = temp.path() / "wal.log";

  CHECK(std::filesystem::exists(wal_path) == false);

  WAL wal(wal_path);
  wal.sync();

  CHECK(std::filesystem::exists(wal_path));
  CHECK(std::filesystem::file_size(wal_path) == 0);
}

TEST_CASE("wal.cpp / appendPut writes a live record and replay reads it back") {
  TempDir temp("append_put");
  WAL wal(temp.path() / "wal.log");

  wal.appendPut("name", "Alice");
  wal.sync();

  std::vector<Record> records = wal.replay();

  REQUIRE(records.size() == 1);
  CHECK(records[0].key == "name");
  CHECK(records[0].val == "Alice");
  CHECK(records[0].tombstone == false);
}

TEST_CASE("wal.cpp / appendDelete writes a tombstone record") {
  TempDir temp("append_delete");
  WAL wal(temp.path() / "wal.log");

  wal.appendDelete("name");
  wal.sync();

  std::vector<Record> records = wal.replay();

  REQUIRE(records.size() == 1);
  CHECK(records[0].key == "name");
  CHECK(records[0].val.empty());
  CHECK(records[0].tombstone == true);
}

TEST_CASE("wal.cpp / replay preserves append order") {
  TempDir temp("append_order");
  WAL wal(temp.path() / "wal.log");

  wal.appendPut("a", "1");
  wal.appendPut("b", "2");
  wal.appendDelete("a");
  wal.sync();

  std::vector<Record> records = wal.replay();

  REQUIRE(records.size() == 3);
  CHECK(records[0].key == "a");
  CHECK(records[0].val == "1");
  CHECK(records[0].tombstone == false);
  CHECK(records[1].key == "b");
  CHECK(records[1].val == "2");
  CHECK(records[1].tombstone == false);
  CHECK(records[2].key == "a");
  CHECK(records[2].val.empty());
  CHECK(records[2].tombstone == true);
}

TEST_CASE("wal.cpp / WAL supports empty keys and empty values") {
  TempDir temp("empty_strings");
  WAL wal(temp.path() / "wal.log");

  wal.appendPut("", "");
  wal.appendPut("empty-value", "");
  wal.sync();

  std::vector<Record> records = wal.replay();

  REQUIRE(records.size() == 2);
  CHECK(records[0].key.empty());
  CHECK(records[0].val.empty());
  CHECK(records[0].tombstone == false);
  CHECK(records[1].key == "empty-value");
  CHECK(records[1].val.empty());
  CHECK(records[1].tombstone == false);
}

TEST_CASE("wal.cpp / WAL preserves values with spaces and embedded null bytes") {
  TempDir temp("binary_strings");
  WAL wal(temp.path() / "wal.log");

  const std::string value("hello world\0tail", 16);
  wal.appendPut("binary key", value);
  wal.sync();

  std::vector<Record> records = wal.replay();

  REQUIRE(records.size() == 1);
  CHECK(records[0].key == "binary key");
  CHECK(records[0].val == value);
  CHECK(records[0].val.size() == 16);
  CHECK(records[0].val[11] == '\0');
  CHECK(records[0].tombstone == false);
}

TEST_CASE("wal.cpp / reset clears existing log records") {
  TempDir temp("reset_clears");
  const auto wal_path = temp.path() / "wal.log";
  WAL wal(wal_path);

  wal.appendPut("old", "value");
  wal.sync();
  CHECK(std::filesystem::file_size(wal_path) > 0);

  wal.reset();

  CHECK(std::filesystem::file_size(wal_path) == 0);
  CHECK(wal.replay().empty());
}

TEST_CASE("wal.cpp / reset leaves WAL reusable") {
  TempDir temp("reset_reusable");
  WAL wal(temp.path() / "wal.log");

  wal.appendPut("old", "value");
  wal.sync();
  wal.reset();
  wal.appendPut("new", "value");
  wal.sync();

  std::vector<Record> records = wal.replay();

  REQUIRE(records.size() == 1);
  CHECK(records[0].key == "new");
  CHECK(records[0].val == "value");
  CHECK(records[0].tombstone == false);
}

TEST_CASE("wal.cpp / replay throws when WAL header is truncated") {
  TempDir temp("truncated_header");
  const auto wal_path = temp.path() / "wal.log";
  writeBytes(wal_path, {'P', '\x01', '\0'});

  WAL wal(wal_path);

  CHECK_THROWS_AS(wal.replay(), std::runtime_error);
}

TEST_CASE("wal.cpp / replay throws when key bytes are truncated") {
  TempDir temp("truncated_key");
  const auto wal_path = temp.path() / "wal.log";
  std::vector<char> bytes;
  appendHeader(bytes, 'P', 4, 0);
  bytes.push_back('k');
  bytes.push_back('e');
  writeBytes(wal_path, bytes);

  WAL wal(wal_path);

  CHECK_THROWS_AS(wal.replay(), std::runtime_error);
}

TEST_CASE("wal.cpp / replay throws when value bytes are truncated") {
  TempDir temp("truncated_value");
  const auto wal_path = temp.path() / "wal.log";
  std::vector<char> bytes;
  appendHeader(bytes, 'P', 3, 5);
  bytes.push_back('k');
  bytes.push_back('e');
  bytes.push_back('y');
  bytes.push_back('v');
  bytes.push_back('a');
  writeBytes(wal_path, bytes);

  WAL wal(wal_path);

  CHECK_THROWS_AS(wal.replay(), std::runtime_error);
}
