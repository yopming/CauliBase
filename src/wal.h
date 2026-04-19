#pragma once

#include <filesystem>
#include <fstream>
#include <vector>

#include "record.h"

/**
 * @brief class of WAL, Write-Ahead Log
 * Before actual written, all data are written to log files.
 * If program crashes, data can be recovered from log files.
 *
 */
class WAL {
public:
  explicit WAL(const std::filesystem::path &path);

  void appendPut(const std::string &key, const std::string &val);
  void appendDelete(const std::string &key);

  // Flush WAL into hard disk
  void sync();

  // Empty WAL file
  // Often called when memtable is full and flushed into SSTable
  void reset();

  // Read all operations from WAL and make them vector of Record
  // When program restarts, this recovers memtable in memory
  std::vector<Record> replay();

private:
  // open WAL file and check status
  void openWALFile();

  // Append a record to the WAL
  void appendRecord(char op, const std::string &key, const std::string &val);

  std::filesystem::path wal_path_; // path for WAL file
  std::ofstream wal_out_;          // out stream to append data into WAL files
};
