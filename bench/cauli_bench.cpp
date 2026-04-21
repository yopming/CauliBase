#include "cauli_base.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct BenchConfig {
  std::size_t operations = 10000;
  std::size_t compact_operations = 2000;
  std::size_t value_size = 64;
  std::string shuffle_mode = "both";
  bool prepare_keys = false;
  std::size_t repeats = 1;
};

struct BenchResult {
  std::string name;
  std::string operation;
  bool shuffling_enabled = false;
  std::size_t operations = 0;
  double total_ms = 0.0;
  double min_ms = 0.0;
  double max_ms = 0.0;
};

class TempDir {
public:
  explicit TempDir(std::string_view name) {
    const auto now = Clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("cauli_bench_" + std::string(name) + "_" + std::to_string(now));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

std::size_t parseSizeArg(const char *arg, std::string_view name) {
  char *end = nullptr;
  const unsigned long long value = std::strtoull(arg, &end, 10);
  if (end == arg || *end != '\0' || value == 0) {
    throw std::invalid_argument("Invalid " + std::string(name) + ": " + arg);
  }
  return static_cast<std::size_t>(value);
}

BenchConfig parseArgs(int argc, char **argv) {
  BenchConfig config;
  if (argc > 1) {
    config.operations = parseSizeArg(argv[1], "operations");
  }
  if (argc > 2) {
    config.compact_operations = parseSizeArg(argv[2], "compact_operations");
  }
  if (argc > 3) {
    config.value_size = parseSizeArg(argv[3], "value_size");
  }
  if (argc > 4) {
    config.shuffle_mode = argv[4];
    if (config.shuffle_mode != "both" && config.shuffle_mode != "shuffle-on" && config.shuffle_mode != "shuffle-off") {
      throw std::invalid_argument("Invalid shuffle mode: " + config.shuffle_mode);
    }
  }
  if (argc > 5) {
    const std::string prepare_mode = argv[5];
    if (prepare_mode == "prepare-keys") {
      config.prepare_keys = true;
    } else if (prepare_mode == "no-prepare") {
      config.prepare_keys = false;
    } else {
      throw std::invalid_argument("Invalid prepare mode: " + prepare_mode);
    }
  }
  if (argc > 6) {
    config.repeats = parseSizeArg(argv[6], "repeats");
  }
  if (argc > 7) {
    throw std::invalid_argument(
        "Usage: cauli_bench [operations] [compact_operations] [value_size] [both|shuffle-on|shuffle-off] [prepare-keys|no-prepare] [repeats]");
  }
  return config;
}

std::string keyFor(std::size_t i) { return "key_" + std::to_string(i); }

std::vector<std::string> keysFor(std::size_t count) {
  std::vector<std::string> keys;
  keys.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    keys.push_back(keyFor(i));
  }
  return keys;
}

std::string valueFor(std::size_t i, std::size_t value_size) {
  std::string value = "value_" + std::to_string(i) + "_";
  if (value.size() < value_size) {
    value.append(value_size - value.size(), 'x');
  }
  return value;
}

template <typename Fn>
BenchResult measure(std::string name, std::size_t operations, Fn &&fn) {
  const auto start = Clock::now();
  fn();
  const auto end = Clock::now();
  const std::chrono::duration<double, std::milli> elapsed = end - start;
  return BenchResult{std::move(name), "", false, operations, elapsed.count(), elapsed.count(), elapsed.count()};
}

BenchResult aggregateMedian(std::vector<BenchResult> samples) {
  if (samples.empty()) {
    throw std::invalid_argument("Benchmark: cannot aggregate empty samples");
  }

  std::sort(samples.begin(), samples.end(),
            [](const BenchResult &lhs, const BenchResult &rhs) { return lhs.total_ms < rhs.total_ms; });

  BenchResult result = samples[samples.size() / 2];
  result.min_ms = samples.front().total_ms;
  result.max_ms = samples.back().total_ms;
  return result;
}

void printResult(const BenchResult &result, const std::vector<BenchResult> &all_results, bool compare_with_plain) {
  const double avg_us = (result.total_ms * 1000.0) / static_cast<double>(result.operations);
  const double ops_per_sec = (static_cast<double>(result.operations) * 1000.0) / result.total_ms;
  std::optional<double> shuffle_over_plain_percent;

  if (compare_with_plain && result.shuffling_enabled) {
    for (const auto &candidate : all_results) {
      if (!candidate.shuffling_enabled && candidate.operation == result.operation && candidate.operations == result.operations &&
          candidate.total_ms > 0.0) {
        shuffle_over_plain_percent = ((result.total_ms - candidate.total_ms) / candidate.total_ms) * 100.0;
        break;
      }
    }
  }

  std::cout << std::left << std::setw(18) << result.name << std::right << std::setw(12) << result.operations
            << std::setw(16) << std::fixed << std::setprecision(3) << result.total_ms << std::setw(16)
            << std::fixed << std::setprecision(3) << avg_us << std::setw(16) << std::fixed << std::setprecision(2)
            << ops_per_sec << std::setw(14) << std::fixed << std::setprecision(3) << result.min_ms << std::setw(14)
            << result.max_ms;

  if (compare_with_plain) {
    if (shuffle_over_plain_percent.has_value()) {
      std::cout << std::setw(18) << std::fixed << std::setprecision(2) << *shuffle_over_plain_percent;
    } else {
      std::cout << std::setw(18) << "-";
    }
  }

  std::cout << "\n";
}

BenchResult withMetadata(BenchResult result, std::string operation, bool shuffling_enabled) {
  result.operation = std::move(operation);
  result.shuffling_enabled = shuffling_enabled;
  return result;
}

BenchResult benchPut(const BenchConfig &config, KeyTransformOptions key_options, std::string_view suffix,
                     bool shuffling_enabled) {
  TempDir temp("put");
  CauliBase db(temp.path(), config.operations + 1, key_options);
  const std::vector<std::string> keys = keysFor(config.operations);
  if (config.prepare_keys) {
    db.prepareKeys(keys);
  }

  return withMetadata(measure("put_" + std::string(suffix), config.operations, [&]() {
    for (std::size_t i = 0; i < config.operations; ++i) {
      db.put(keys[i], valueFor(i, config.value_size));
    }
  }), "put", shuffling_enabled);
}

BenchResult benchGetFromMemtable(const BenchConfig &config, KeyTransformOptions key_options, std::string_view suffix,
                                 bool shuffling_enabled) {
  TempDir temp("get_memtable");
  CauliBase db(temp.path(), config.operations + 1, key_options);
  const std::vector<std::string> keys = keysFor(config.operations);
  if (config.prepare_keys) {
    db.prepareKeys(keys);
  }
  for (std::size_t i = 0; i < config.operations; ++i) {
    db.put(keys[i], valueFor(i, config.value_size));
  }

  std::size_t found = 0;
  auto result = withMetadata(measure("get_mem_" + std::string(suffix), config.operations, [&]() {
    for (std::size_t i = 0; i < config.operations; ++i) {
      if (db.get(keys[i]).has_value()) {
        ++found;
      }
    }
  }), "get_mem", shuffling_enabled);

  if (found != config.operations) {
    throw std::runtime_error("get_memtable benchmark did not find all keys");
  }
  return result;
}

BenchResult benchGetFromSSTable(const BenchConfig &config, KeyTransformOptions key_options, std::string_view suffix,
                                bool shuffling_enabled) {
  TempDir temp("get_sstable");
  const std::vector<std::string> keys = keysFor(config.operations);
  {
    CauliBase db(temp.path(), config.operations + 1, key_options);
    if (config.prepare_keys) {
      db.prepareKeys(keys);
    }
    for (std::size_t i = 0; i < config.operations; ++i) {
      db.put(keys[i], valueFor(i, config.value_size));
    }
    db.flush();
  }

  CauliBase db(temp.path(), config.operations + 1, key_options);
  if (config.prepare_keys) {
    db.prepareKeys(keys);
  }
  std::size_t found = 0;
  auto result = withMetadata(measure("get_sst_" + std::string(suffix), config.operations, [&]() {
    for (std::size_t i = 0; i < config.operations; ++i) {
      if (db.get(keys[i]).has_value()) {
        ++found;
      }
    }
  }), "get_sst", shuffling_enabled);

  if (found != config.operations) {
    throw std::runtime_error("get_sstable benchmark did not find all keys");
  }
  return result;
}

BenchResult benchDel(const BenchConfig &config, KeyTransformOptions key_options, std::string_view suffix,
                     bool shuffling_enabled) {
  TempDir temp("del");
  CauliBase db(temp.path(), config.operations + 1, key_options);
  const std::vector<std::string> keys = keysFor(config.operations);
  if (config.prepare_keys) {
    db.prepareKeys(keys);
  }
  for (std::size_t i = 0; i < config.operations; ++i) {
    db.put(keys[i], valueFor(i, config.value_size));
  }

  return withMetadata(measure("del_" + std::string(suffix), config.operations, [&]() {
    for (std::size_t i = 0; i < config.operations; ++i) {
      db.del(keys[i]);
    }
  }), "del", shuffling_enabled);
}

BenchResult benchCompact(const BenchConfig &config, KeyTransformOptions key_options, std::string_view suffix,
                         bool shuffling_enabled) {
  TempDir temp("compact");
  CauliBase db(temp.path(), 1, key_options);
  const std::vector<std::string> keys = keysFor(config.compact_operations);
  if (config.prepare_keys) {
    db.prepareKeys(keys);
  }

  for (std::size_t i = 0; i < config.compact_operations; ++i) {
    db.put(keys[i], valueFor(i, config.value_size));
  }
  for (std::size_t i = 0; i < config.compact_operations; i += 2) {
    db.del(keys[i]);
  }

  return withMetadata(
      measure("compact_" + std::string(suffix), config.compact_operations + (config.compact_operations + 1) / 2,
              [&]() { db.compact(); }),
      "compact", shuffling_enabled);
}

void printUsage(const char *program) {
  std::cout << "Usage: " << program
            << " [operations] [compact_operations] [value_size] [both|shuffle-on|shuffle-off] [prepare-keys|no-prepare] [repeats]\n"
            << "  operations: number of put/get/del operations, default 10000\n"
            << "  compact_operations: number of keys prepared for compact, default 2000\n"
            << "  value_size: generated value size in bytes, default 64\n"
            << "  shuffle mode: compare both modes by default\n"
            << "  prepare-keys: precompute storage keys before measured operations, default no-prepare\n"
            << "  repeats: run each benchmark multiple times and report the median, default 1\n";
}

void appendSuite(std::vector<BenchResult> &results, const BenchConfig &config, bool shuffling_enabled) {
  const KeyTransformOptions key_options{shuffling_enabled};
  const std::string_view suffix = shuffling_enabled ? "shuffle" : "plain";

  std::vector<BenchResult> put_samples;
  std::vector<BenchResult> get_mem_samples;
  std::vector<BenchResult> get_sst_samples;
  std::vector<BenchResult> del_samples;
  std::vector<BenchResult> compact_samples;
  put_samples.reserve(config.repeats);
  get_mem_samples.reserve(config.repeats);
  get_sst_samples.reserve(config.repeats);
  del_samples.reserve(config.repeats);
  compact_samples.reserve(config.repeats);

  for (std::size_t i = 0; i < config.repeats; ++i) {
    put_samples.push_back(benchPut(config, key_options, suffix, shuffling_enabled));
    get_mem_samples.push_back(benchGetFromMemtable(config, key_options, suffix, shuffling_enabled));
    get_sst_samples.push_back(benchGetFromSSTable(config, key_options, suffix, shuffling_enabled));
    del_samples.push_back(benchDel(config, key_options, suffix, shuffling_enabled));
    compact_samples.push_back(benchCompact(config, key_options, suffix, shuffling_enabled));
  }

  results.push_back(aggregateMedian(std::move(put_samples)));
  results.push_back(aggregateMedian(std::move(get_mem_samples)));
  results.push_back(aggregateMedian(std::move(get_sst_samples)));
  results.push_back(aggregateMedian(std::move(del_samples)));
  results.push_back(aggregateMedian(std::move(compact_samples)));
}

} // namespace

int main(int argc, char **argv) {
  try {
    const BenchConfig config = parseArgs(argc, argv);

    std::cout << "CauliBase benchmark\n"
              << "operations=" << config.operations << ", compact_operations=" << config.compact_operations
              << ", value_size=" << config.value_size << ", shuffle_mode=" << config.shuffle_mode
              << ", prepare_keys=" << (config.prepare_keys ? "true" : "false") << ", repeats=" << config.repeats
              << "\n\n";

    std::vector<BenchResult> results;
    if (config.shuffle_mode == "both" || config.shuffle_mode == "shuffle-on") {
      appendSuite(results, config, true);
    }
    if (config.shuffle_mode == "both" || config.shuffle_mode == "shuffle-off") {
      appendSuite(results, config, false);
    }

    const bool compare_with_plain = (config.shuffle_mode == "both");
    std::cout << std::left << std::setw(18) << "benchmark" << std::right << std::setw(12) << "ops"
              << std::setw(16) << "median_ms" << std::setw(16) << "avg_us/op" << std::setw(16) << "ops/sec"
              << std::setw(14) << "min_ms" << std::setw(14) << "max_ms";
    if (compare_with_plain) {
      std::cout << std::setw(18) << "shuffle_vs_plain%";
    }
    std::cout << "\n";
    std::cout << std::string(compare_with_plain ? 124 : 106, '-') << "\n";

    for (const auto &result : results) {
      printResult(result, results, compare_with_plain);
    }
  } catch (const std::exception &e) {
    std::cerr << "Benchmark error: " << e.what() << "\n";
    printUsage(argv[0]);
    return 1;
  }

  return 0;
}
