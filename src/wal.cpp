#include "wal.h"

#include <cstdint>
#include <stdexcept>

/**
 * @brief Construct a new WAL::WAL object
 *
 * @param path
 */
WAL::WAL(const std::filesystem::path &path) : path_(path) {
  // open file as mode of "binary" and "append"
  out_.open(path_, std::ios::binary | std::ios::app);

  // if file opening fails, throws exception
  if (!out_) {
    throw std::runtime_error("Failed to open WAL: " + path_.string());
  }
}

void WAL::appendPut(const std::string &key, const std::string &val) {
  appendRecord('P', key, val); // 'P' for Put operation
}

void WAL::appendDelete(const std::string &key) {
  appendRecord('D', key, ""); // 'D' for delete
}

void WAL::sync() {
  out_.flush();
  if (!out_) {
    throw std::runtime_error("Failed to flush WAL");
  }
}

/// PRIVATE

// actual function to append a record
void WAL::appendRecord(char op, const std::string &key, const std::string &val) {
  uint32_t key_len = static_cast<uint32_t>(key.size());
  uint32_t val_len = static_cast<uint32_t>(val.size());

  // write 1 byte for op
  out_.write(reinterpret_cast<const char *>(&op), sizeof(op));
  // write 4 bytes for key len
  out_.write(reinterpret_cast<const char *>(&key_len), sizeof(key_len));
  // write 4 bytes for val len
  out_.write(reinterpret_cast<char *>(&val_len), sizeof(val_len));

  // write content of 'key'
  out_.write(key.data(), static_cast<std::streamsize>(key.size()));
  // write content of 'val'
  out_.write(key.data(), static_cast<std::streamsize>(val.size()));

  if (!out_) {
    throw std::runtime_error("Failed to append WAL record");
  }
}

// replay all operations in WAL, and make a vector of Records
std::vector<Record> WAL::replay(const std::filesystem::path &path) {
  std::vector<Record> result; // save recovered records

  // if WAL file does not exist
  if (!std::filesystem::exists(path)) {
    return result;
  }

  // open WAL file with mode of "binary"
  std::ifstream in(path, std::ios::binary);
  // open failed
  if (!in) {
    throw std::runtime_error("Failed to open WAL for replay: " + path.string());
  }

  // keep reading
  while (true) {
    char op = 0;
    uint32_t key_len = 0;
    uint32_t val_len = 0;
    in.read(reinterpret_cast<char *>(&op), sizeof(op));

    // if read fails, typically indicates end of file
    if (!in) {
      break;
    }

    in.read(reinterpret_cast<char *>(&key_len), sizeof(key_len));
    in.read(reinterpret_cast<char *>(&val_len), sizeof(val_len));

    if (!in) {
      throw std::runtime_error("Corrupted WAL header");
    }

    Record rec;
    rec.key.resize(key_len);
    rec.val.resize(val_len);
    rec.tombstone = (op == 'D');

    in.read(rec.key.data(), static_cast<std::streamsize>(key_len));
    in.read(rec.val.data(), static_cast<std::streamsize>(val_len));
    if (!in) {
      throw std::runtime_error("Corrupted WAL body");
    }

    result.push_back(std::move(rec));
  }

  return result;
}

void WAL::reset(const std::filesystem::path &path) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("Failed to truncate WAL: " + path.string());
  }
}
