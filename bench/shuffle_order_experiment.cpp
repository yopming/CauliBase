#include "key_transform.h"
#include "record.h"
#include "sstable.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct OrderResult {
  std::string name;
  std::size_t key_count = 0;
  long double spearman_rho = 0.0L;
  std::filesystem::path sstable_path;
  std::filesystem::path csv_path;
};

std::size_t parseSizeArg(const char *arg, std::string_view name) {
  char *end = nullptr;
  const unsigned long long value = std::strtoull(arg, &end, 10);
  if (end == arg || *end != '\0' || value == 0) {
    throw std::invalid_argument("Invalid " + std::string(name) + ": " + arg);
  }
  return static_cast<std::size_t>(value);
}

std::string orderedKey(std::size_t i, std::size_t width) {
  std::ostringstream out;
  out << "key_" << std::setw(static_cast<int>(width)) << std::setfill('0') << i;
  return out.str();
}

std::size_t decimalWidth(std::size_t value) {
  std::size_t width = 1;
  while (value >= 10) {
    value /= 10;
    ++width;
  }
  return width;
}

std::string hexEncode(const std::string &value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(value.size() * 2);
  for (unsigned char ch : value) {
    out.push_back(digits[ch >> 4]);
    out.push_back(digits[ch & 0x0f]);
  }
  return out;
}

long double spearmanForPhysicalOrder(const std::vector<std::size_t> &logical_ranks_by_physical_rank) {
  const auto n = static_cast<long double>(logical_ranks_by_physical_rank.size());
  if (logical_ranks_by_physical_rank.size() < 2) {
    return 1.0L;
  }

  long double sum_squared_distance = 0.0L;
  for (std::size_t physical_rank = 0; physical_rank < logical_ranks_by_physical_rank.size(); ++physical_rank) {
    const auto distance = static_cast<long double>(logical_ranks_by_physical_rank[physical_rank]) -
                          static_cast<long double>(physical_rank);
    sum_squared_distance += distance * distance;
  }

  return 1.0L - (6.0L * sum_squared_distance) / (n * (n * n - 1.0L));
}

OrderResult runExperiment(const std::string &name, std::size_t key_count, const std::filesystem::path &output_dir,
                          bool shuffled) {
  const std::size_t width = decimalWidth(key_count);
  const KeyTransform key_transform(KeyTransformOptions{true});
  const auto sstable_path = output_dir / (name + ".sst");
  const auto csv_path = output_dir / (name + "_order.csv");

  std::map<std::string, Record> records;
  std::unordered_map<std::string, std::size_t> logical_rank_by_storage_key;
  std::unordered_map<std::string, std::string> external_key_by_storage_key;
  logical_rank_by_storage_key.reserve(key_count);
  external_key_by_storage_key.reserve(key_count);

  for (std::size_t i = 1; i <= key_count; ++i) {
    const std::string external_key = orderedKey(i, width);
    const std::string storage_key = shuffled ? key_transform.storageKey(external_key) : external_key;
    if (logical_rank_by_storage_key.find(storage_key) != logical_rank_by_storage_key.end()) {
      throw std::runtime_error("Duplicate storage key generated for experiment.");
    }

    const std::size_t logical_rank = i - 1;
    logical_rank_by_storage_key.emplace(storage_key, logical_rank);
    external_key_by_storage_key.emplace(storage_key, external_key);
    records.emplace(storage_key, Record{storage_key, "value", false});
  }

  SSTable sstable(sstable_path);
  sstable.writeFromMap(records);

  const std::map<std::string, Record> physical_records = sstable.loadAll();
  std::vector<std::size_t> logical_ranks_by_physical_rank;
  logical_ranks_by_physical_rank.reserve(physical_records.size());

  std::ofstream csv(csv_path, std::ios::trunc);
  if (!csv.is_open()) {
    throw std::runtime_error("Failed to create CSV: " + csv_path.string());
  }
  csv << "physical_rank,logical_rank,external_key,storage_key_hex\n";

  std::size_t physical_rank = 0;
  for (const auto &[storage_key, record] : physical_records) {
    (void)record;
    const auto logical_it = logical_rank_by_storage_key.find(storage_key);
    const auto external_it = external_key_by_storage_key.find(storage_key);
    if (logical_it == logical_rank_by_storage_key.end() || external_it == external_key_by_storage_key.end()) {
      throw std::runtime_error("SSTable physical order contains an unknown key.");
    }

    logical_ranks_by_physical_rank.push_back(logical_it->second);
    csv << physical_rank << ',' << logical_it->second << ',' << external_it->second << ',' << hexEncode(storage_key)
        << '\n';
    ++physical_rank;
  }

  if (!csv) {
    throw std::runtime_error("Failed to write CSV: " + csv_path.string());
  }

  return OrderResult{name, key_count, spearmanForPhysicalOrder(logical_ranks_by_physical_rank), sstable_path, csv_path};
}

void printResult(const OrderResult &result) {
  std::cout << std::left << std::setw(12) << result.name << std::right << std::setw(12) << result.key_count
            << std::setw(18) << std::fixed << std::setprecision(6) << static_cast<double>(result.spearman_rho)
            << "  " << result.csv_path.string() << '\n';
}

void printUsage(const char *program) {
  std::cout << "Usage: " << program << " [key_count] [output_dir]\n"
            << "  key_count: number of ordered keys, default 20000\n"
            << "  output_dir: directory for SSTables and CSV files, default ./shuffle_order_experiment\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::size_t key_count = 20000;
    std::filesystem::path output_dir = std::filesystem::current_path() / "shuffle_order_experiment";

    if (argc > 1) {
      key_count = parseSizeArg(argv[1], "key_count");
    }
    if (argc > 2) {
      output_dir = argv[2];
    }
    if (argc > 3) {
      throw std::invalid_argument("Too many arguments.");
    }

    std::filesystem::create_directories(output_dir);

    const OrderResult baseline = runExperiment("baseline", key_count, output_dir, false);
    const OrderResult shuffled = runExperiment("shuffled", key_count, output_dir, true);

    std::cout << "SSTable physical key order experiment\n"
              << "output_dir=" << output_dir.string() << "\n\n";
    std::cout << std::left << std::setw(12) << "mode" << std::right << std::setw(12) << "keys" << std::setw(18)
              << "spearman_rho"
              << "  csv\n";
    std::cout << std::string(84, '-') << '\n';
    printResult(baseline);
    printResult(shuffled);
  } catch (const std::exception &e) {
    std::cerr << "Experiment error: " << e.what() << '\n';
    printUsage(argv[0]);
    return 1;
  }

  return 0;
}
