#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>

#include "record.h"

/**
 * @brief Minimum SStable, its essential is a readonly & ordered KV file
 * @details When flushing, write KV from memtable to SStable;
 *          When get, read KVs from SSTable
 * @details sstable file format:
 *          [header]
 */
class SSTable {
public:
  explicit SSTable(std::filesystem::path path);

  const std::filesystem::path &path() const;

  // write into a SSTable file from a memtable in memory
  void writeFromMap(const std::map<std::string, Record> &memtable);

  // find a key in the SSTable
  std::optional<Record> get(const std::string &target_key) const;

  // read entire SSTable into a map, used when compaction
  std::map<std::string, Record> loadAll() const;

private:
  // read one record from filestream which maintains pointer
  Record readRecord(std::ifstream& in) const;

  std::filesystem::path sstable_path_; // sstable file path
};
