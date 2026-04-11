#pragma once

#include <string>

/**
 * @brief 
 * struct for one record in database

 * tombstone: false means "regular record", true means "deleted record".
 * In LSM, delete is not physical deletion, write one "tombstone" first.
 */
struct Record {
  std::string key;
  std::string value;
  bool tombstone = false; // delete flag
};
