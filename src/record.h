#pragma once

#include <string>

/**
 * @brief struct for one record in database
 * 
 * tombstone: false means "regular record", true means "deleted record".
 * In CauliBase, delete is not phyiscal deletion, mark tombstone as "true" first.
 */
struct Record {
  std::string key;
  std::string val;
  bool tombstone = false; // delete flag
};
