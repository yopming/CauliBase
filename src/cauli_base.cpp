#include "cauli_base.h"
#include "sstable.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>

/**
 * @brief Construct a new Cauli Base:: Cauli Base object
 *        1. create database directory
 *        2. initialize WAL
 *        3. initialize memtable
 *        4. recover memtable from WAL
 *        5. load existing sstables
 *
 * @param db_dir
 * @param memtable_limit
 */
CauliBase::CauliBase(const std::filesystem::path &db_dir, std::size_t memtable_limit, KeyTransformOptions key_options)
    : db_dir_(db_dir),
      wal_path_(db_dir_ / "wal.log"),
      memtable_limit_(memtable_limit),
      key_transform_(key_options) {
  // make sure the directory exists
  std::filesystem::create_directories(db_dir_);

  wal_ = std::make_unique<WAL>(wal_path_); // 1. initialize WAL
  key_cache_.reserve(memtable_limit_);
  recoverFromWAL();                        // 2. recover memtable from WAL
  loadSSTables();                          // 3. load SSTables
}

/**
 * @brief PUT KV, update WAL and memtable
 *
 * @param key
 * @param val
 */
void CauliBase::put(const std::string &key, const std::string &val) {
  const std::string &storage_key = storageKeyFor(key);
  wal_->appendPut(storage_key, val);                        // 1. append PUT(K,V) into WAL
  wal_->sync();                                             // 2. persist WAL
  memtable_[storage_key] = Record{storage_key, val, false}; // 3. update memtable_ (ram's latest status)
  maybeFlush();                                             // 4. see if flush needed
}

/**
 * @brief DEL key
 *
 * @param key
 */
void CauliBase::del(const std::string &key) {
  const std::string &storage_key = storageKeyFor(key);
  wal_->appendDelete(storage_key);                        // 1. append DEL(K) into WAL
  wal_->sync();                                           // 2. persist WAL
  memtable_[storage_key] = Record{storage_key, "", true}; // 3. update memtable_
  maybeFlush();                                           // 4. see if flush needed
}

/**
 * @brief Query
 *
 * @param key
 * @return std::optional<std::string>
 */
std::optional<std::string> CauliBase::get(const std::string &key) const {
  const std::string &storage_key = storageKeyFor(key);

  // search memtable_ (in memory) first
  auto it_ram = memtable_.find(storage_key);
  if (it_ram != memtable_.end()) {
    // if found in ram, check 'tombstone'
    if (it_ram->second.tombstone) {
      return std::nullopt; // if tombstone is true, marked as deleted
    } else {
      return it_ram->second.val;
    }
  }

  // search sstables_ (in disk) if key not found in memory
  for (auto it_disk = sstables_.rbegin(); it_disk != sstables_.rend(); ++it_disk) {
    auto record = it_disk->get(storage_key);
    if (record.has_value()) {
      // if key found, check 'tombstone' further
      if (record->tombstone) {
        return std::nullopt;
      } else {
        return record->val;
      }
    }
  }

  return std::nullopt; // if not found in disk
}

void CauliBase::prepareKey(const std::string &key) const { (void)storageKeyFor(key); }

void CauliBase::prepareKeys(const std::vector<std::string> &keys) const {
  key_cache_.reserve(key_cache_.size() + keys.size());
  for (const auto &key : keys) {
    prepareKey(key);
  }
}

void CauliBase::flush() {
  if (memtable_.empty()) return; // empty memtable_, nothing to flush

  // get path for next new sst
  auto sst_path = nextSSTablePath();
  SSTable sstable(sst_path);
  sstable.writeFromMap(memtable_);

  // update sstables_, a vector of sst in disk
  sstables_.emplace_back(sstable);

  // clear memtable_, and reset WAL
  memtable_.clear();
  wal_->reset();
}

/**
 * @brief SSTables compaction
 */
void CauliBase::compact() {
  std::map<std::string, Record> merged;

  // 1. merge all SSTables
  for (const auto &sst : sstables_) {
    std::map<std::string, Record> data = sst.loadAll();
    for (auto &[k, record] : data) {
      merged[k] = std::move(record);
    }
  }

  // 2. memtable is always newest, so it overwrites SSTable data
  for (const auto &[k, record] : memtable_) {
    merged[k] = record;
  }

  // 3. remove tombstones
  for (auto it = merged.begin(); it != merged.end();) {
    if (it->second.tombstone) {
      it = merged.erase(it);
    } else {
      ++it;
    }
  }

  // 4. write new SSTable first
  std::optional<std::filesystem::path> new_path;
  if (!merged.empty()) {
    auto out = nextSSTablePath();
    SSTable sst(out);
    sst.writeFromMap(merged);
    new_path = out;
  }

  // 5. remove old SSTables
  for (const auto &sst : sstables_) {
    std::error_code e;
    std::filesystem::remove(sst.path(), e);
    if (e) throw std::runtime_error("CauliBase: failed to remove sstable: " + sst.path().string());
  }

  // 6. update in-memory sstables_
  sstables_.clear();
  if (new_path.has_value()) {
    sstables_.emplace_back(*new_path);
  }

  // 7. clear memtable and reset WAL
  memtable_.clear();
  wal_->reset();
}

void CauliBase::printDebug() const {
  std::cout << "----- Debug ----- \n";
  std::cout << "Memtable size: " << memtable_.size() << "\n";
  std::cout << "SSTable count: " << sstables_.size() << "\n";

  for (std::size_t i = 0; i < sstables_.size(); ++i) {
    std::cout << " [" << i << "] " << sstables_[i].path().string() << "\n";
  }

  std::cout << "-----------------" << std::endl;
}

/// PRIVATE

/**
 * @brief Check if memtable size reaches memtable_limit_, and flush if so.
 *        Currently, use number of records in memtable_ to trigger the flush,
 *        will use actual size in future.
 */
void CauliBase::maybeFlush() {
  if (memtable_.size() >= memtable_limit_) {
    flush();
  }
}

/**
 * @brief Recover memtable from WAL after crashes
 */
void CauliBase::recoverFromWAL() {
  std::vector<Record> records = wal_->replay();
  for (const auto &record : records) {
    memtable_[record.key] = record;
  }
}

/**
 * @brief load all sstables into memory for faster query.
 */
void CauliBase::loadSSTables() {
  std::vector<std::filesystem::path> files;

  // scan the db_dir_ directory, put all *.sst file into a vector
  for (const auto &entry : std::filesystem::directory_iterator(db_dir_)) {
    if (!entry.is_regular_file()) continue; // ignore special file (folder, symlink, etc)

    auto path = entry.path();
    auto name = path.filename().string();
    if (path.extension() == ".sst") {
      files.push_back(path);
    }
  }

  // sorting, and put them into sstables_ vector
  std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) {
    try {
      uint64_t id_a = std::stoull(a.stem().string());
      uint64_t id_b = std::stoull(b.stem().string());
      return id_a < id_b;
    } catch (...) {
      return a < b; // fallback
    }
  });

  for (const auto &file : files) {
    sstables_.emplace_back(file);
  }
}

const std::string &CauliBase::storageKeyFor(const std::string &key) const {
  auto cached = key_cache_.find(key);
  if (cached != key_cache_.end()) {
    return cached->second;
  }

  auto inserted = key_cache_.emplace(key, key_transform_.storageKey(key));
  return inserted.first->second;
}

/**
 * @brief generate file name for next sst.
 *        sst naming rules: <id>.sst, id has width of 6 (1 million max).
 *        in production, compaction will control number of sst
 *
 * @return std::filesystem::path
 */
std::filesystem::path CauliBase::nextSSTablePath() const {
  uint64_t max_id = 0;

  for (const auto &sst : sstables_) {
    if (sst.path().extension() != ".sst") continue;

    // file name without extension
    auto stem = sst.path().stem().string(); // e.g. "012345"

    try {
      uint64_t id = std::stoull(stem); // "str" to int
      if (id > max_id) max_id = id;
    } catch (const std::exception &) {
      // ignore non-numerical file name
      continue;
    }
  }

  std::ostringstream oss;
  oss << std::setw(6) << std::setfill('0') << (max_id + 1) << ".sst";
  return db_dir_ / oss.str();
}
