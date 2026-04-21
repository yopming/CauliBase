#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "key_transform.h"
#include "record.h"
#include "sstable.h"
#include "wal.h"

/**
 * @brief
 *
 * @note memtable_ (MemTable), a ordered data structure in memory
 *       - handle all write requests (put/delete)
 *       - all keys are ordered
 * @note wal_ (WAL, Write-Ahead Log), an append-only log file (append in orders)
 *       - data in memtable_ may be lost (in memory), we need wal_
 * @note sstables_ (Sorted String Tables), immutable data files in disk
 *
 * @details [flush] memtable_ => sstable_, when size reaches the limit
 * @details [write] orders: 1. wal_, 2. memtable_, 3. flush to sstables_
 * @details [query] orders: 1. memtable_, 2. sstables_
 *
 * @details memtable_ : faster write
 *          wal_      : recovery from crashes
 *          sstable_  : persistent storage
 *
 * @details disk directory(WAL: recently data not flushed; SSTable: new sst file for every flush):
 *          db_dir/
 *               |-- wal.log
 *               |-- sstable_1.sst
 *               |-- sstable_2.sst
 *               |-- ...
 *
 * @details memtable structure is `std::map<std::string, Record>`, a map of Record
 */
class CauliBase {
public:
  /**
   * @brief Construct a new Cauli Base object
   *
   * @param db_dir
   * @param memtable_limit
   */
  explicit CauliBase(const std::filesystem::path &db_dir, std::size_t memtable_limit = 1024,
                     KeyTransformOptions key_options = {});

  void put(const std::string &key, const std::string &val);
  void del(const std::string &key);
  std::optional<std::string> get(const std::string &key) const;
  void prepareKey(const std::string &key) const;
  void prepareKeys(const std::vector<std::string> &keys) const;

  void flush();
  void compact();          // merge multiple SSTables
  void printDebug() const; // display debug info (sstables names)

private:
  void maybeFlush();     // check if memtable exceeds limit, if so flush to SSTable
  void recoverFromWAL(); // recover memtable from WAL after crash
  void loadSSTables();   // load all SSTables into memory for faster get()
  const std::string &storageKeyFor(const std::string &key) const;

  // generate next SSTable file path
  std::filesystem::path nextSSTablePath() const;

  std::filesystem::path db_dir_;           // data directory
  std::filesystem::path wal_path_;         // path for WAL file
  std::unique_ptr<WAL> wal_;               // unique instance of WAL object
  std::map<std::string, Record> memtable_; // memtable in memory
  std::vector<SSTable> sstables_;          // sstables_ in-memory
  std::size_t memtable_limit_;             // when trigger flush
  KeyTransform key_transform_;             // normalize, permute, and optionally shuffle external keys
  mutable std::unordered_map<std::string, std::string> key_cache_;
};
