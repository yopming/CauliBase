#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <vector>

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
  struct IndexEntry {
    std::string key;
    std::uint64_t offset = 0;
  };

  struct Metadata {
    std::vector<IndexEntry> index;
    std::vector<std::uint8_t> bloom_bits;
    std::uint64_t bloom_bit_count = 0;
    std::uint32_t bloom_hash_count = 0;
  };

  // read one record from filestream which maintains pointer
  Record readRecord(std::ifstream &in) const;
  std::optional<Metadata> loadMetadata() const;
  const std::optional<Metadata> &metadata() const;
  bool bloomMayContain(const Metadata &metadata, const std::string &key) const;

  std::filesystem::path sstable_path_; // sstable file path
  mutable std::optional<std::optional<Metadata>> metadata_cache_;
};
