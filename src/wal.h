#pragma once

#include "record.h"
#include <filesystem>
#include <fstream>
#include <vector>

/**
 * @brief class of WAL, Write-Ahead Log
 * Before actual written, all data are written into log file. 
 * Even program crashes, data can be recovered from log files.
 */
class WAL {
public:
  explicit WAL(const std::filesystem::path& path);

  void appendPut(const std::string& key, const std::string& val);
  void appendDelete(const std::string& key);

  // Flush WAL into hard disk
  void sync();

  // Read all operations from WAL and make them vector of Records
  // When program restarts, this recovers memtable in memory
  static std::vector<Record> replay(const std::filesystem::path& path);

  // Empty WAL file
  // Often called when memtable is flushed into SSTable
  static void reset(const std::filesystem::path& path);

private:
  /**
   * @brief Append a record to the WAL
   * 
   * @param op operation type, 'P' for put, 'D' for delete
   * @param key 
   * @param val
   */
  void appendRecord(char op, const std::string& key, const std::string& val);

  std::filesystem::path path_; // file path for WAL file
  std::ofstream out_; // out stream to append data into WAL files
};
