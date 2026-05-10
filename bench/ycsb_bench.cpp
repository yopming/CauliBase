#include "cauli_base.h"
#include "ycsb_workload.h"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct BenchConfig {
  std::vector<ycsb::CoreWorkload> workloads = {ycsb::CoreWorkload::A, ycsb::CoreWorkload::B, ycsb::CoreWorkload::C,
                                               ycsb::CoreWorkload::F};
  std::size_t record_count = 100000;
  std::size_t operation_count = 100000;
  std::size_t value_size = 100;
  ycsb::RequestDistribution distribution = ycsb::RequestDistribution::Zipfian;
  bool shuffling_enabled = false;
  std::uint64_t seed = 1;
  std::size_t memtable_limit = 1024;
};

struct BenchResult {
  ycsb::CoreWorkload workload = ycsb::CoreWorkload::A;
  std::size_t records = 0;
  std::size_t operations = 0;
  double load_ms = 0.0;
  double transaction_ms = 0.0;
  std::size_t found = 0;
};

class TempDir {
public:
  explicit TempDir(std::string_view name) {
    const auto now = Clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("cauli_ycsb_" + std::string(name) + "_" + std::to_string(now));
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

std::uint64_t parseSeedArg(const char *arg) {
  char *end = nullptr;
  const unsigned long long value = std::strtoull(arg, &end, 10);
  if (end == arg || *end != '\0') {
    throw std::invalid_argument("Invalid seed: " + std::string(arg));
  }
  return static_cast<std::uint64_t>(value);
}

std::vector<ycsb::CoreWorkload> parseWorkloads(const std::string &value) {
  if (value == "all") {
    return {ycsb::CoreWorkload::A, ycsb::CoreWorkload::B, ycsb::CoreWorkload::C, ycsb::CoreWorkload::F};
  }
  return {ycsb::parseCoreWorkload(value)};
}

bool parseShuffleMode(const std::string &value) {
  if (value == "shuffle-on") {
    return true;
  }
  if (value == "shuffle-off") {
    return false;
  }
  throw std::invalid_argument("Invalid shuffle mode: " + value);
}

std::size_t keyId(const std::string &key) {
  return static_cast<std::size_t>(std::strtoull(key.substr(4).c_str(), nullptr, 10));
}

double elapsedMs(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

bool executeOperation(CauliBase &db, const ycsb::Operation &operation) {
  switch (operation.type) {
  case ycsb::OperationType::Read:
    return db.get(operation.key).has_value();
  case ycsb::OperationType::Update:
  case ycsb::OperationType::Insert:
    db.put(operation.key, operation.value);
    return true;
  case ycsb::OperationType::ReadModifyWrite: {
    const bool found = db.get(operation.key).has_value();
    db.put(operation.key, operation.value);
    return found;
  }
  case ycsb::OperationType::Scan: {
    bool any_found = false;
    const std::size_t first_key = keyId(operation.key);
    for (std::size_t i = 0; i < operation.scan_length; ++i) {
      any_found = db.get(ycsb::WorkloadGenerator::keyFor(first_key + i)).has_value() || any_found;
    }
    return any_found;
  }
  }
  return false;
}

BenchResult runWorkload(const BenchConfig &bench_config, ycsb::CoreWorkload workload) {
  ycsb::WorkloadConfig workload_config;
  workload_config.workload = workload;
  workload_config.record_count = bench_config.record_count;
  workload_config.operation_count = bench_config.operation_count;
  workload_config.value_size = bench_config.value_size;
  workload_config.request_distribution = bench_config.distribution;
  workload_config.seed = bench_config.seed;

  ycsb::WorkloadGenerator generator(workload_config);
  TempDir temp(ycsb::workloadName(workload));
  CauliBase db(temp.path(), bench_config.memtable_limit, KeyTransformOptions{bench_config.shuffling_enabled});

  const auto load_start = Clock::now();
  for (const auto &operation : generator.loadOperations()) {
    executeOperation(db, operation);
  }
  const auto load_end = Clock::now();

  std::size_t found = 0;
  const auto transaction_start = Clock::now();
  for (std::size_t i = 0; i < workload_config.operation_count; ++i) {
    if (executeOperation(db, generator.nextTransaction())) {
      ++found;
    }
  }
  const auto transaction_end = Clock::now();

  return BenchResult{workload, workload_config.record_count, workload_config.operation_count, elapsedMs(load_start, load_end),
                     elapsedMs(transaction_start, transaction_end), found};
}

BenchConfig parseArgs(int argc, char **argv) {
  BenchConfig config;
  if (argc > 1) {
    config.workloads = parseWorkloads(argv[1]);
  }
  if (argc > 2) {
    config.record_count = parseSizeArg(argv[2], "record_count");
  }
  if (argc > 3) {
    config.operation_count = parseSizeArg(argv[3], "operation_count");
  }
  if (argc > 4) {
    config.value_size = parseSizeArg(argv[4], "value_size");
  }
  if (argc > 5) {
    config.distribution = ycsb::parseRequestDistribution(argv[5]);
  }
  if (argc > 6) {
    config.shuffling_enabled = parseShuffleMode(argv[6]);
  }
  if (argc > 7) {
    config.seed = parseSeedArg(argv[7]);
  }
  if (argc > 8) {
    config.memtable_limit = parseSizeArg(argv[8], "memtable_limit");
  }
  if (argc > 9) {
    throw std::invalid_argument("Too many arguments.");
  }
  return config;
}

void printUsage(const char *program) {
  std::cout << "Usage: " << program
            << " [all|a|b|c|d|e|f] [record_count] [operation_count] [value_size] [uniform|zipfian|latest] [shuffle-on|shuffle-off] [seed] [memtable_limit]\n"
            << "  all runs YCSB workloads A, B, C, and F.\n"
            << "  Defaults: all 100000 100000 100 zipfian shuffle-off 1 1024\n";
}

void printResult(const BenchResult &result) {
  const double load_ops_sec = static_cast<double>(result.records) * 1000.0 / result.load_ms;
  const double transaction_ops_sec = static_cast<double>(result.operations) * 1000.0 / result.transaction_ms;
  const double transaction_us = result.transaction_ms * 1000.0 / static_cast<double>(result.operations);

  std::cout << std::left << std::setw(12) << ycsb::workloadName(result.workload) << std::right << std::setw(12)
            << result.records << std::setw(12) << result.operations << std::setw(14) << std::fixed
            << std::setprecision(3) << result.load_ms << std::setw(14) << std::fixed << std::setprecision(2)
            << load_ops_sec << std::setw(14) << std::fixed << std::setprecision(3) << result.transaction_ms
            << std::setw(14) << std::fixed << std::setprecision(3) << transaction_us << std::setw(14) << std::fixed
            << std::setprecision(2) << transaction_ops_sec << std::setw(12) << result.found << '\n';
}

} // namespace

int main(int argc, char **argv) {
  try {
    const BenchConfig config = parseArgs(argc, argv);

    std::cout << "CauliBase YCSB benchmark\n"
              << "record_count=" << config.record_count << ", operation_count=" << config.operation_count
              << ", value_size=" << config.value_size
              << ", shuffling_enabled=" << (config.shuffling_enabled ? "true" : "false")
              << ", seed=" << config.seed << ", memtable_limit=" << config.memtable_limit << "\n\n";

    std::cout << std::left << std::setw(12) << "workload" << std::right << std::setw(12) << "records"
              << std::setw(12) << "ops" << std::setw(14) << "load_ms" << std::setw(14) << "load_ops/s"
              << std::setw(14) << "txn_ms" << std::setw(14) << "txn_us/op" << std::setw(14) << "txn_ops/s"
              << std::setw(12) << "found"
              << "\n";
    std::cout << std::string(118, '-') << '\n';

    for (const auto workload : config.workloads) {
      printResult(runWorkload(config, workload));
    }
  } catch (const std::exception &e) {
    std::cerr << "YCSB benchmark error: " << e.what() << '\n';
    printUsage(argv[0]);
    return 1;
  }

  return 0;
}
