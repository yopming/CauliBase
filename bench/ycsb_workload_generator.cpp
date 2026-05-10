#include "ycsb_workload.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

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

void printOperation(const ycsb::Operation &operation) {
  std::cout << ycsb::operationName(operation.type) << ' ' << operation.key;
  if (operation.type == ycsb::OperationType::Insert || operation.type == ycsb::OperationType::Update ||
      operation.type == ycsb::OperationType::ReadModifyWrite) {
    std::cout << ' ' << operation.value;
  }
  if (operation.type == ycsb::OperationType::Scan) {
    std::cout << ' ' << operation.scan_length;
  }
  std::cout << '\n';
}

void printUsage(const char *program) {
  std::cout << "Usage: " << program
            << " [a|b|c|d|e|f] [record_count] [operation_count] [value_size] [uniform|zipfian|latest] [seed]\n"
            << "  Emits a YCSB-style load phase followed by a transaction phase.\n"
            << "  Defaults: workload=a, record_count=10000, operation_count=10000, value_size=100, distribution=zipfian, seed=1\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    ycsb::WorkloadConfig config;
    if (argc > 1) {
      config.workload = ycsb::parseCoreWorkload(argv[1]);
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
      config.request_distribution = ycsb::parseRequestDistribution(argv[5]);
    }
    if (argc > 6) {
      config.seed = parseSeedArg(argv[6]);
    }
    if (argc > 7) {
      throw std::invalid_argument("Too many arguments.");
    }

    ycsb::WorkloadGenerator generator(config);
    std::cout << "# YCSB " << ycsb::workloadName(config.workload) << '\n';
    std::cout << "# load\n";
    for (const auto &operation : generator.loadOperations()) {
      printOperation(operation);
    }
    std::cout << "# transactions\n";
    for (std::size_t i = 0; i < config.operation_count; ++i) {
      printOperation(generator.nextTransaction());
    }
  } catch (const std::exception &e) {
    std::cerr << "YCSB workload generator error: " << e.what() << '\n';
    printUsage(argv[0]);
    return 1;
  }

  return 0;
}
