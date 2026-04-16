#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <record.h>

/**
 * @brief SSTable = Sorted String Table, immutable disk file after sorting
 *
 */
class SSTable {
public:
  explicit SSTable(std::filesystem::path path);

  const std::filesystem::path &path() const;

  // write memtable in memory into a SSTable file
  static void writeFromMap(const std::filesystem::path &path, const std::map<std::string, Record> &memtable);

  // find a key in this SSTable
  std::optional<Record> get(const std::string &target_key) const;

  // read entire SSTable into a map, used when compaction
  std::map<std::string, Record> loadAll() const;

private:
  std::filesystem::path path_; // sstable file path
};
