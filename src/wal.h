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

private:
  /**
   * @brief Append a record to the WAL
   * 
   * @param op operation type, 'P' for put, 'D' for delete
   * @param key 
   * @param value 
   */
  void appendRecord(char op, const std::string& key, const std::string& value);

  std::filesystem::path path_; // file path for WAL file
  std::ofstream out_; // out stream to append data into WAL files
};
