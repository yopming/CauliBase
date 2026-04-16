#include "sstable.h"

#include <cstdint>
#include <fstream>
#include <ios>
#include <optional>
#include <stdexcept>

namespace {

// helper function, read one Record from filestream
Record readRecord(std::ifstream& in) {
  uint32_t key_len = 0;
  uint32_t val_len = 0;
  uint8_t tombstone = 0;

  in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
  in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
  in.read(reinterpret_cast<char*>(&tombstone), sizeof(tombstone));

  if (!in) {
    throw std::runtime_error("Corrupted SSTable record header");
  }

  Record rec;
  rec.key.resize(key_len);
  rec.val.resize(val_len);
  rec.tombstone = (tombstone != 0); // 1 means deleted, 0 means regular

  in.read(rec.key.data(), static_cast<std::streamsize>(key_len));
  in.read(rec.val.data(), static_cast<std::streamsize>(val_len));

  if (!in) {
    throw std::runtime_error("Corrupted SSTable record body");
  }

  return rec;
}
}

SSTable::SSTable(std::filesystem::path path) : path_(std::move(path)) {}

const std::filesystem::path& SSTable::Path() const {
  return path_;
}

void SSTable::writeFromMap(const std::filesystem::path &path, const std::map<std::string, Record> &memtable) {
  // truncate if file exists
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("Failed to create SSTable: " + path.string());
  }

  // number of records in this SSTable
  uint64_t record_count = static_cast<uint64_t>(memtable.size());
  // write number of records into SSTable header
  out.write(reinterpret_cast<const char*>(&record_count), sizeof(record_count));

  // visit every record in the map
  // map is sorted, so written SSTable is also sorted by key
  for (const auto& [k, rec]: memtable) {
    (void) k; // rec.key has same info

    uint32_t key_len = static_cast<uint32_t>(rec.key.size());
    uint32_t val_len = static_cast<uint32_t>(rec.val.size());
    uint8_t tombstone = rec.tombstone ? 1 : 0; // true -> 1, false -> 0

    out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    out.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
    out.write(reinterpret_cast<const char*>(&tombstone), sizeof(tombstone));

    out.write(rec.key.data(), static_cast<std::streamsize>(rec.key.size()));
    out.write(rec.val.data(), static_cast<std::streamsize>(rec.val.size()));

    if (!out) {
      throw std::runtime_error("Failed to write SSTable");
    }
  }
}

std::optional<Record> SSTable::get(const std::string& target_key) const {
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    throw std::runtime_error("Failed to open SSTable: " + path_.string());
  }

  uint64_t record_count = 0;
  in.read(reinterpret_cast<char*>(&record_count), sizeof(record_count));
  if (!in) {
    throw std::runtime_error("Corrupted SSTable header: " + path_.string());
  }

  // scan record in SSTable one by one
  for (uint64_t i = 0; i < record_count; ++i) {
    Record rec = readRecord(in);
    if (rec.key == target_key) {
      return rec;
    }

    // sorted. If current key is larger than target_key, stop
    if (rec.key > target_key) {
      return std::nullopt;
    }
  }

  return std::nullopt; // not found
}

// load entire SStable into memory
std::map<std::string, Record> SSTable::loadAll() const {
  std::ifstream in(path_, std::ios::binary);
  if (!in) { 
    throw std::runtime_error("Failed to open SSTable: " + path_.string());
  }

  uint64_t record_count = 0;
  in.read(reinterpret_cast<char*>(&record_count), sizeof(record_count));
  if (!in) {
    throw std::runtime_error("Corrupted SSTable header: " + path_.string());
  }

  std::map<std::string, Record> result;
  for (uint64_t i = 0; i < record_count; ++i) {
    
  }
}
