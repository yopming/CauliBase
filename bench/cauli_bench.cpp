#include "cauli_base.h"

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
};

struct BenchResult {
  std::string name;
  std::size_t operations = 0;
  double total_ms = 0.0;
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
    throw std::invalid_argument("Usage: cauli_bench [operations] [compact_operations] [value_size]");
  }
  return config;
}

std::string keyFor(std::size_t i) { return "key_" + std::to_string(i); }

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
  return BenchResult{std::move(name), operations, elapsed.count()};
}

void printResult(const BenchResult &result) {
  const double avg_us = (result.total_ms * 1000.0) / static_cast<double>(result.operations);
  const double ops_per_sec = (static_cast<double>(result.operations) * 1000.0) / result.total_ms;

  std::cout << std::left << std::setw(18) << result.name << std::right << std::setw(12) << result.operations
            << std::setw(16) << std::fixed << std::setprecision(3) << result.total_ms << std::setw(16)
            << std::fixed << std::setprecision(3) << avg_us << std::setw(16) << std::fixed << std::setprecision(2)
            << ops_per_sec << "\n";
}

BenchResult benchPut(const BenchConfig &config) {
  TempDir temp("put");
  CauliBase db(temp.path(), config.operations + 1);

  return measure("put", config.operations, [&]() {
    for (std::size_t i = 0; i < config.operations; ++i) {
      db.put(keyFor(i), valueFor(i, config.value_size));
    }
  });
}

BenchResult benchGetFromMemtable(const BenchConfig &config) {
  TempDir temp("get_memtable");
  CauliBase db(temp.path(), config.operations + 1);
  for (std::size_t i = 0; i < config.operations; ++i) {
    db.put(keyFor(i), valueFor(i, config.value_size));
  }

  std::size_t found = 0;
  auto result = measure("get_memtable", config.operations, [&]() {
    for (std::size_t i = 0; i < config.operations; ++i) {
      if (db.get(keyFor(i)).has_value()) {
        ++found;
      }
    }
  });

  if (found != config.operations) {
    throw std::runtime_error("get_memtable benchmark did not find all keys");
  }
  return result;
}

BenchResult benchGetFromSSTable(const BenchConfig &config) {
  TempDir temp("get_sstable");
  {
    CauliBase db(temp.path(), config.operations + 1);
    for (std::size_t i = 0; i < config.operations; ++i) {
      db.put(keyFor(i), valueFor(i, config.value_size));
    }
    db.flush();
  }

  CauliBase db(temp.path(), config.operations + 1);
  std::size_t found = 0;
  auto result = measure("get_sstable", config.operations, [&]() {
    for (std::size_t i = 0; i < config.operations; ++i) {
      if (db.get(keyFor(i)).has_value()) {
        ++found;
      }
    }
  });

  if (found != config.operations) {
    throw std::runtime_error("get_sstable benchmark did not find all keys");
  }
  return result;
}

BenchResult benchDel(const BenchConfig &config) {
  TempDir temp("del");
  CauliBase db(temp.path(), config.operations + 1);
  for (std::size_t i = 0; i < config.operations; ++i) {
    db.put(keyFor(i), valueFor(i, config.value_size));
  }

  return measure("del", config.operations, [&]() {
    for (std::size_t i = 0; i < config.operations; ++i) {
      db.del(keyFor(i));
    }
  });
}

BenchResult benchCompact(const BenchConfig &config) {
  TempDir temp("compact");
  CauliBase db(temp.path(), 1);

  for (std::size_t i = 0; i < config.compact_operations; ++i) {
    db.put(keyFor(i), valueFor(i, config.value_size));
  }
  for (std::size_t i = 0; i < config.compact_operations; i += 2) {
    db.del(keyFor(i));
  }

  return measure("compact", config.compact_operations + (config.compact_operations + 1) / 2, [&]() { db.compact(); });
}

void printUsage(const char *program) {
  std::cout << "Usage: " << program << " [operations] [compact_operations] [value_size]\n"
            << "  operations: number of put/get/del operations, default 10000\n"
            << "  compact_operations: number of keys prepared for compact, default 2000\n"
            << "  value_size: generated value size in bytes, default 64\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    const BenchConfig config = parseArgs(argc, argv);

    std::cout << "CauliBase benchmark\n"
              << "operations=" << config.operations << ", compact_operations=" << config.compact_operations
              << ", value_size=" << config.value_size << "\n\n";

    std::cout << std::left << std::setw(18) << "benchmark" << std::right << std::setw(12) << "ops"
              << std::setw(16) << "total_ms" << std::setw(16) << "avg_us/op" << std::setw(16) << "ops/sec"
              << "\n";
    std::cout << std::string(78, '-') << "\n";

    const std::vector<BenchResult> results = {
        benchPut(config),
        benchGetFromMemtable(config),
        benchGetFromSSTable(config),
        benchDel(config),
        benchCompact(config),
    };

    for (const auto &result : results) {
      printResult(result);
    }
  } catch (const std::exception &e) {
    std::cerr << "Benchmark error: " << e.what() << "\n";
    printUsage(argv[0]);
    return 1;
  }

  return 0;
}
