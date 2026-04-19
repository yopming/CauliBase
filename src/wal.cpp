#include "wal.h"

#include <fstream>
#include <ios>
#include <stdexcept>

/**
 * @brief Construct a new WAL::WAL object
 *
 * @param path
 */
WAL::WAL(const std::filesystem::path &path) : wal_path_(path) {
  // wal_out_ declared, open a file to bind the wal file
  openWALFile();
}

void WAL::appendPut(const std::string &key, const std::string &val) { appendRecord('P', key, val); }

void WAL::appendDelete(const std::string &key) { appendRecord('D', key, ""); }

void WAL::sync() {
  wal_out_.flush();
  if (!wal_out_) throw std::runtime_error("WAL: failed to flush WAL file.");
}

void WAL::reset() {
  // close and truncate WAL file
  wal_out_.close();
  std::ofstream temp(wal_path_, std::ios::binary | std::ios::trunc);
  openWALFile(); // open and check
}

std::vector<Record> WAL::replay() {
  // open WAL file with mode 'binary'
  std::ifstream in(wal_path_, std::ios::binary);
  if (!in) throw std::runtime_error("WAL: failed to open WAL file for replay.");

  // prepare a vector of Record
  std::vector<Record> results;

  while (true) {
    char op = 0;
    std::uint32_t key_size = 0;
    std::uint32_t val_size = 0;

    // read op
    in.read(reinterpret_cast<char *>(&op), sizeof(op));
    if (in.eof()) break; // EOF
    if (!in) throw std::runtime_error("WAL: failed to read WAL file during replay.");

    // read key_size and val_size
    in.read(reinterpret_cast<char *>(&key_size), sizeof(key_size));
    in.read(reinterpret_cast<char *>(&val_size), sizeof(val_size));
    if (!in) throw std::runtime_error("WAL: corrupted WAL file header.");

    // read key
    std::string key(key_size, '\0');
    if (key_size > 0) {
      in.read(key.data(), static_cast<std::streamsize>(key_size));
      if (!in) throw std::runtime_error("WAL: corrupted WAL key.");
    }

    // read val
    std::string val(val_size, '\0');
    if (val_size > 0) {
      in.read(val.data(), static_cast<std::streamsize>(val_size));
      if (!in) throw std::runtime_error("WAL: corrupted WAL val.");
    }

    // put record into the vector
    bool tombstone = (op == 'D');
    Record record = {key, val, tombstone};
    results.push_back(record);
  }

  return results;
}

/// PRIVATE

/**
 * @brief Open WAL file
 * @details used when WAL class is initialized or when WAL file needs to be reset
 * @details open with mode 'binary' and 'append'
 */
void WAL::openWALFile() {
  wal_out_.open(wal_path_, std::ios::binary | std::ios::app);
  if (!wal_out_) throw std::runtime_error("WAL: failed to open wal file: " + wal_path_.string());
}

/**
 * @brief Common function to append one record
 * @details Format of one record:
 *          [op:1 byte | key_size:4 bytes | val_size:4 bytes | key bytes | val bytes]
 * @details op: 'P' for put, 'D' for delete
 *
 * @param op operation type
 * @param key key
 * @param val value
 */
void WAL::appendRecord(char op, const std::string &key, const std::string &val) {
  // check if ofstream is okay to write
  if (!wal_out_.is_open()) {
    throw std::runtime_error("WAL: wal file is not open: " + wal_path_.string());
  }

  // convert key_size and val_size to 4 bytes
  const uint32_t key_size = static_cast<uint32_t>(key.size());
  const uint32_t val_size = static_cast<uint32_t>(val.size());

  // writing
  wal_out_.write(reinterpret_cast<const char *>(&op), sizeof(op));             // 1 byte for op
  wal_out_.write(reinterpret_cast<const char *>(&key_size), sizeof(key_size)); // key_size
  wal_out_.write(reinterpret_cast<const char *>(&val_size), sizeof(val_size)); // val_size
  wal_out_.write(key.data(), static_cast<std::streamsize>(key.size()));        // key
  wal_out_.write(val.data(), static_cast<std::streamsize>(val.size()));        // val

  if (!wal_out_) {
    throw std::runtime_error("WAL: failed to append record");
  }
}
