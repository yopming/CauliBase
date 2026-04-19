#include "sstable.h"

#include <cstdint>
#include <fstream>
#include <ios>
#include <optional>
#include <stdexcept>

/// Initializer for one sstable
SSTable::SSTable(std::filesystem::path path) : sstable_path_(path) {}

/// Getter for the system path of sst file. Structure of data is the same as in WAL
const std::filesystem::path &SSTable::path() const { return sstable_path_; }

void SSTable::writeFromMap(const std::map<std::string, Record> &memtable) {
  // truncate if file exists
  std::ofstream out(sstable_path_, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) throw std::runtime_error("SSTable: failed to create sstable file: " + sstable_path_.string());

  // number of records in memtable, and write it to SStable header
  uint64_t record_count = static_cast<uint64_t>(memtable.size());
  out.write(reinterpret_cast<const char *>(&record_count), sizeof(record_count));

  // visit every record in memtable. Since memtable is ordered, written sst is also sorted by key
  for (const auto &[key, record] : memtable) {
    (void)key; // record.key has same information

    uint32_t key_size = static_cast<uint32_t>(record.key.size());
    uint32_t val_size = static_cast<uint32_t>(record.val.size());
    uint8_t tombstone = record.tombstone ? 1 : 0; // true -> 1, false -> 0

    // write to sst
    out.write(reinterpret_cast<const char *>(&key_size), sizeof(key_size));
    out.write(reinterpret_cast<const char *>(&val_size), sizeof(val_size));
    out.write(reinterpret_cast<const char *>(&tombstone), sizeof(tombstone));

    if (key_size > 0) {
      out.write(record.key.data(), static_cast<std::streamsize>(record.key.size()));
    }
    if (val_size > 0) {
      out.write(record.val.data(), static_cast<std::streamsize>(record.val.size()));
    }

    if (!out) throw std::runtime_error("SSTable: failed to write.");
  }
}

std::optional<Record> SSTable::get(const std::string &target_key) const {
  std::ifstream in(sstable_path_, std::ios::binary);
  if (!in) throw std::runtime_error("SSTable: failed to open the sstable file " + sstable_path_.string());

  // read number of records in sst
  uint64_t record_count = 0;
  in.read(reinterpret_cast<char *>(&record_count), sizeof(record_count));
  if (!in) throw std::runtime_error("SSTable: corrupted sstable file header in " + sstable_path_.string());

  // check all records in SStable one by one
  for (uint64_t i = 0; i < record_count; ++i) {
    Record record = readRecord(in);
    if (record.key == target_key) return record;

    // if current record.key is larger than target, no need to continue (since sorted)
    if (record.key > target_key) break;
  }

  return std::nullopt;
}

std::map<std::string, Record> SSTable::loadAll() const {
  std::ifstream in(sstable_path_, std::ios::binary);
  if (!in) throw std::runtime_error("SSTable: failed to open sstable file " + sstable_path_.string());

  // get number of records in sst
  uint64_t record_count = 0;
  in.read(reinterpret_cast<char *>(&record_count), sizeof(record_count));
  if (!in) throw std::runtime_error("SSTable: corrupted sstable file header in " + sstable_path_.string());

  std::map<std::string, Record> results;
  for (uint64_t i = 0; i < record_count; ++i) {
    Record record = readRecord(in);
    results[record.key] = std::move(record);
  }

  return results;
}

/// PRIVATE

/**
 * @brief Read one record from ifstream, the read pointer will be updated automatically
 *
 * @param in ifstream can maintain a read pointer
 * @return Record
 */
Record SSTable::readRecord(std::ifstream &in) const {
  uint32_t key_size = 0;
  uint32_t val_size = 0;
  uint8_t tombstone = 0;

  in.read(reinterpret_cast<char *>(&key_size), sizeof(key_size));
  in.read(reinterpret_cast<char *>(&val_size), sizeof(val_size));
  in.read(reinterpret_cast<char *>(&tombstone), sizeof(tombstone));
  if (!in) throw std::runtime_error("SSTable: corrupted sstable header.");

  Record record;
  record.key.resize(key_size);
  record.val.resize(val_size);
  record.tombstone = (tombstone != 0);

  in.read(record.key.data(), static_cast<std::streamsize>(key_size));
  in.read(record.val.data(), static_cast<std::streamsize>(val_size));
  if (!in) throw std::runtime_error("SSTable: corrupted record body.");

  return record;
}
