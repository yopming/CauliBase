#include <doctest/doctest.h>

#include "sstable.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

class TempDir {
public:
  explicit TempDir(const std::string &name) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("cauli_sstable_test_" + name + "_" + std::to_string(now));
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

template <typename T>
void appendPod(std::vector<char> &bytes, const T &value) {
  const auto *ptr = reinterpret_cast<const char *>(&value);
  bytes.insert(bytes.end(), ptr, ptr + sizeof(T));
}

void appendRecordHeader(std::vector<char> &bytes, std::uint32_t key_size, std::uint32_t val_size,
                        std::uint8_t tombstone) {
  appendPod(bytes, key_size);
  appendPod(bytes, val_size);
  appendPod(bytes, tombstone);
}

} // namespace

TEST_CASE("sstable.h / SSTable stores its file path") {
  TempDir temp("path");
  const auto path = temp.path() / "000001.sst";

  SSTable sstable(path);

  CHECK(sstable.path() == path);
}

TEST_CASE("sstable.cpp / writeFromMap creates an SSTable file") {
  TempDir temp("create_file");
  const auto path = temp.path() / "000001.sst";
  SSTable sstable(path);

  std::map<std::string, Record> memtable = {
      {"name", Record{"name", "Alice", false}},
  };
  sstable.writeFromMap(memtable);

  CHECK(std::filesystem::exists(path));
  CHECK(std::filesystem::file_size(path) > 0);
}

TEST_CASE("sstable.cpp / get returns an existing live record") {
  TempDir temp("get_live");
  SSTable sstable(temp.path() / "000001.sst");
  std::map<std::string, Record> memtable = {
      {"name", Record{"name", "Alice", false}},
  };
  sstable.writeFromMap(memtable);

  std::optional<Record> record = sstable.get("name");

  REQUIRE(record.has_value());
  CHECK(record->key == "name");
  CHECK(record->val == "Alice");
  CHECK(record->tombstone == false);
}

TEST_CASE("sstable.cpp / get returns nullopt for missing keys") {
  TempDir temp("missing_key");
  SSTable sstable(temp.path() / "000001.sst");
  std::map<std::string, Record> memtable = {
      {"alpha", Record{"alpha", "1", false}},
      {"charlie", Record{"charlie", "3", false}},
  };
  sstable.writeFromMap(memtable);

  CHECK(sstable.get("bravo").has_value() == false);
  CHECK(sstable.get("zulu").has_value() == false);
}

TEST_CASE("sstable.cpp / get preserves tombstone records") {
  TempDir temp("tombstone");
  SSTable sstable(temp.path() / "000001.sst");
  std::map<std::string, Record> memtable = {
      {"deleted", Record{"deleted", "", true}},
  };
  sstable.writeFromMap(memtable);

  std::optional<Record> record = sstable.get("deleted");

  REQUIRE(record.has_value());
  CHECK(record->key == "deleted");
  CHECK(record->val.empty());
  CHECK(record->tombstone == true);
}

TEST_CASE("sstable.cpp / writeFromMap and get preserve empty values") {
  TempDir temp("empty_value");
  SSTable sstable(temp.path() / "000001.sst");
  std::map<std::string, Record> memtable = {
      {"empty", Record{"empty", "", false}},
  };
  sstable.writeFromMap(memtable);

  std::optional<Record> record = sstable.get("empty");

  REQUIRE(record.has_value());
  CHECK(record->key == "empty");
  CHECK(record->val.empty());
  CHECK(record->tombstone == false);
}

TEST_CASE("sstable.cpp / writeFromMap and get preserve values with embedded null bytes") {
  TempDir temp("binary_value");
  SSTable sstable(temp.path() / "000001.sst");
  const std::string value("hello\0sst", 9);
  std::map<std::string, Record> memtable = {
      {"binary", Record{"binary", value, false}},
  };
  sstable.writeFromMap(memtable);

  std::optional<Record> record = sstable.get("binary");

  REQUIRE(record.has_value());
  CHECK(record->val == value);
  CHECK(record->val.size() == 9);
  CHECK(record->val[5] == '\0');
}

TEST_CASE("sstable.cpp / loadAll returns all records sorted by key") {
  TempDir temp("load_all");
  SSTable sstable(temp.path() / "000001.sst");
  std::map<std::string, Record> memtable = {
      {"charlie", Record{"charlie", "3", false}},
      {"alpha", Record{"alpha", "1", false}},
      {"bravo", Record{"bravo", "", true}},
  };
  sstable.writeFromMap(memtable);

  std::map<std::string, Record> loaded = sstable.loadAll();

  REQUIRE(loaded.size() == 3);
  CHECK(loaded.at("alpha").val == "1");
  CHECK(loaded.at("bravo").tombstone == true);
  CHECK(loaded.at("charlie").val == "3");
}

TEST_CASE("sstable.cpp / writeFromMap supports an empty memtable") {
  TempDir temp("empty_memtable");
  SSTable sstable(temp.path() / "000001.sst");

  sstable.writeFromMap({});

  CHECK(std::filesystem::exists(sstable.path()));
  CHECK(sstable.loadAll().empty());
  CHECK(sstable.get("missing").has_value() == false);
}

TEST_CASE("sstable.cpp / loadAll throws when SSTable header is truncated") {
  TempDir temp("truncated_file_header");
  const auto path = temp.path() / "000001.sst";
  writeBytes(path, {'\x01', '\0', '\0'});
  SSTable sstable(path);

  CHECK_THROWS_AS(sstable.loadAll(), std::runtime_error);
}

TEST_CASE("sstable.cpp / get throws when record header is truncated") {
  TempDir temp("truncated_record_header");
  const auto path = temp.path() / "000001.sst";
  std::vector<char> bytes;
  appendPod<std::uint64_t>(bytes, 1);
  appendPod<std::uint32_t>(bytes, 3);
  writeBytes(path, bytes);
  SSTable sstable(path);

  CHECK_THROWS_AS(sstable.get("key"), std::runtime_error);
}

TEST_CASE("sstable.cpp / loadAll throws when record body is truncated") {
  TempDir temp("truncated_record_body");
  const auto path = temp.path() / "000001.sst";
  std::vector<char> bytes;
  appendPod<std::uint64_t>(bytes, 1);
  appendRecordHeader(bytes, 3, 5, 0);
  bytes.push_back('k');
  bytes.push_back('e');
  bytes.push_back('y');
  bytes.push_back('v');
  writeBytes(path, bytes);
  SSTable sstable(path);

  CHECK_THROWS_AS(sstable.loadAll(), std::runtime_error);
}
