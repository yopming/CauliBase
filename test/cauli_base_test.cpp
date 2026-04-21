#include <doctest/doctest.h>

#include "cauli_base.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

namespace {

class TempDir {
public:
  explicit TempDir(const std::string &name) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("cauli_base_test_" + name + "_" + std::to_string(now));
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

std::size_t countSSTables(const std::filesystem::path &path) {
  std::size_t count = 0;
  if (!std::filesystem::exists(path)) {
    return count;
  }

  for (const auto &entry : std::filesystem::directory_iterator(path)) {
    if (entry.is_regular_file() && entry.path().extension() == ".sst") {
      ++count;
    }
  }
  return count;
}

std::string readBinaryFile(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("cauli_base.h / constructor creates database directory and WAL") {
  TempDir temp("constructor");
  const auto db_path = temp.path() / "db";

  CHECK(std::filesystem::exists(db_path) == false);

  CauliBase db(db_path, 10);

  CHECK(std::filesystem::is_directory(db_path));
  CHECK(std::filesystem::exists(db_path / "wal.log"));
  CHECK(db.get("missing").has_value() == false);
}

TEST_CASE("cauli_base.cpp / put and get store a value in the memtable") {
  TempDir temp("put_get");
  CauliBase db(temp.path(), 10);

  db.put("name", "Alice");

  std::optional<std::string> value = db.get("name");
  REQUIRE(value.has_value());
  CHECK(*value == "Alice");
}

TEST_CASE("cauli_base.cpp / put overwrites an existing key") {
  TempDir temp("overwrite");
  CauliBase db(temp.path(), 10);

  db.put("name", "Alice");
  db.put("name", "Bob");

  std::optional<std::string> value = db.get("name");
  REQUIRE(value.has_value());
  CHECK(*value == "Bob");
}

TEST_CASE("cauli_base.cpp / del hides an existing key") {
  TempDir temp("delete");
  CauliBase db(temp.path(), 10);

  db.put("name", "Alice");
  db.del("name");

  CHECK(db.get("name").has_value() == false);
}

TEST_CASE("cauli_base.cpp / get returns nullopt for unknown keys") {
  TempDir temp("missing");
  CauliBase db(temp.path(), 10);

  CHECK(db.get("missing").has_value() == false);
}

TEST_CASE("cauli_base.cpp / WAL recovery restores unflushed puts and deletes") {
  TempDir temp("wal_recovery");
  {
    CauliBase db(temp.path(), 10);
    db.put("alpha", "1");
    db.put("bravo", "2");
    db.del("alpha");
  }

  CauliBase recovered(temp.path(), 10);

  CHECK(recovered.get("alpha").has_value() == false);
  REQUIRE(recovered.get("bravo").has_value());
  CHECK(*recovered.get("bravo") == "2");
}

TEST_CASE("cauli_base.cpp / flush persists memtable records to SSTable and clears WAL") {
  TempDir temp("flush");
  {
    CauliBase db(temp.path(), 10);
    db.put("alpha", "1");
    db.put("bravo", "2");
    db.flush();

    CHECK(db.get("alpha").value_or("") == "1");
    CHECK(db.get("bravo").value_or("") == "2");
  }

  CHECK(countSSTables(temp.path()) == 1);
  CHECK(std::filesystem::file_size(temp.path() / "wal.log") == 0);

  CauliBase reopened(temp.path(), 10);
  CHECK(reopened.get("alpha").value_or("") == "1");
  CHECK(reopened.get("bravo").value_or("") == "2");
}

TEST_CASE("cauli_base.cpp / memtable limit automatically flushes records") {
  TempDir temp("auto_flush");
  CauliBase db(temp.path(), 2);

  db.put("alpha", "1");
  CHECK(countSSTables(temp.path()) == 0);

  db.put("bravo", "2");

  CHECK(countSSTables(temp.path()) == 1);
  CHECK(db.get("alpha").value_or("") == "1");
  CHECK(db.get("bravo").value_or("") == "2");
}

TEST_CASE("cauli_base.cpp / newer SSTable value wins over older SSTable value") {
  TempDir temp("newer_sstable_wins");
  {
    CauliBase db(temp.path(), 1);
    db.put("name", "Alice");
    db.put("name", "Bob");
  }

  CHECK(countSSTables(temp.path()) == 2);

  CauliBase reopened(temp.path(), 10);
  CHECK(reopened.get("name").value_or("") == "Bob");
}

TEST_CASE("cauli_base.cpp / flushed tombstone hides older SSTable value") {
  TempDir temp("flushed_tombstone");
  {
    CauliBase db(temp.path(), 1);
    db.put("name", "Alice");
    db.del("name");
  }

  CauliBase reopened(temp.path(), 10);

  CHECK(reopened.get("name").has_value() == false);
}

TEST_CASE("cauli_base.cpp / compact keeps latest values and removes deleted keys") {
  TempDir temp("compact");
  {
    CauliBase db(temp.path(), 1);
    db.put("alpha", "old");
    db.put("bravo", "2");
    db.put("alpha", "new");
    db.del("bravo");

    CHECK(countSSTables(temp.path()) == 4);

    db.compact();

    CHECK(db.get("alpha").value_or("") == "new");
    CHECK(db.get("bravo").has_value() == false);
  }

  CHECK(countSSTables(temp.path()) == 1);
  CHECK(std::filesystem::file_size(temp.path() / "wal.log") == 0);

  CauliBase reopened(temp.path(), 10);
  CHECK(reopened.get("alpha").value_or("") == "new");
  CHECK(reopened.get("bravo").has_value() == false);
}

TEST_CASE("cauli_base.cpp / compact removes all SSTables when every key is deleted") {
  TempDir temp("compact_all_deleted");
  {
    CauliBase db(temp.path(), 1);
    db.put("alpha", "1");
    db.del("alpha");
    db.compact();
  }

  CHECK(countSSTables(temp.path()) == 0);

  CauliBase reopened(temp.path(), 10);
  CHECK(reopened.get("alpha").has_value() == false);
}

TEST_CASE("cauli_base.cpp / flush on empty memtable is a no-op") {
  TempDir temp("empty_flush");
  CauliBase db(temp.path(), 10);

  db.flush();

  CHECK(countSSTables(temp.path()) == 0);
}

TEST_CASE("cauli_base.cpp / key shuffling stores transformed keys and keeps operations working") {
  TempDir temp("shuffle_keys");
  {
    CauliBase db(temp.path(), 10, KeyTransformOptions{true});
    db.put("customer:123456789", "Alice");
    db.flush();

    CHECK(db.get("customer:123456789").value_or("") == "Alice");
    db.del("customer:123456789");
    CHECK(db.get("customer:123456789").has_value() == false);
  }

  const std::string sstable_bytes = readBinaryFile(temp.path() / "000001.sst");
  CHECK(sstable_bytes.find("customer:123456789") == std::string::npos);
  CHECK(sstable_bytes.find(KeyTransform(KeyTransformOptions{true}).storageKey("customer:123456789")) != std::string::npos);

  CauliBase reopened(temp.path(), 10, KeyTransformOptions{true});
  CHECK(reopened.get("customer:123456789").has_value() == false);
}

TEST_CASE("cauli_base.cpp / shuffling can be disabled for comparison") {
  TempDir temp("shuffle_off");
  {
    CauliBase db(temp.path(), 10, KeyTransformOptions{false});
    db.put("customer:123456789", "Alice");
    db.flush();
  }

  const std::string sstable_bytes = readBinaryFile(temp.path() / "000001.sst");
  CHECK(sstable_bytes.find("customer:123456789") == std::string::npos);
  CHECK(sstable_bytes.find(KeyTransform(KeyTransformOptions{false}).storageKey("customer:123456789")) != std::string::npos);

  CauliBase reopened(temp.path(), 10, KeyTransformOptions{false});
  CHECK(reopened.get("customer:123456789").value_or("") == "Alice");
}

TEST_CASE("cauli_base.cpp / prepared shuffled keys keep put get del behavior") {
  TempDir temp("prepared_shuffle_keys");
  CauliBase db(temp.path(), 10, KeyTransformOptions{true});
  const std::vector<std::string> keys = {"alpha", "bravo", "charlie"};

  db.prepareKeys(keys);
  db.put("alpha", "1");
  db.put("bravo", "2");
  db.del("alpha");

  CHECK(db.get("alpha").has_value() == false);
  CHECK(db.get("bravo").value_or("") == "2");
  CHECK(db.get("charlie").has_value() == false);
}

TEST_CASE("cauli_base.cpp / printDebug prints memtable and SSTable counts") {
  TempDir temp("debug");
  CauliBase db(temp.path(), 10);
  db.put("alpha", "1");

  std::ostringstream output;
  auto *old_buffer = std::cout.rdbuf(output.rdbuf());
  db.printDebug();
  std::cout.rdbuf(old_buffer);

  CHECK(output.str().find("Memtable size: 1") != std::string::npos);
  CHECK(output.str().find("SSTable count: 0") != std::string::npos);
}
