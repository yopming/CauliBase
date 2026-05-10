#include <doctest/doctest.h>

#include "ycsb_workload.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>

namespace {

std::size_t keyId(const std::string &key) {
  return static_cast<std::size_t>(std::strtoull(key.substr(4).c_str(), nullptr, 10));
}

} // namespace

TEST_CASE("YCSB workload generator creates a load phase of inserts") {
  ycsb::WorkloadConfig config;
  config.record_count = 3;
  config.value_size = 16;

  ycsb::WorkloadGenerator generator(config);
  const auto operations = generator.loadOperations();

  REQUIRE(operations.size() == 3);
  CHECK(operations[0].type == ycsb::OperationType::Insert);
  CHECK(operations[0].key == "user00000000000000000000");
  CHECK(operations[1].key == "user00000000000000000001");
  CHECK(operations[2].value.size() == 16);
}

TEST_CASE("YCSB core workload C generates only reads") {
  ycsb::WorkloadConfig config;
  config.workload = ycsb::CoreWorkload::C;
  config.request_distribution = ycsb::RequestDistribution::Uniform;
  config.record_count = 100;
  config.operation_count = 1000;

  ycsb::WorkloadGenerator generator(config);
  for (std::size_t i = 0; i < config.operation_count; ++i) {
    const auto operation = generator.nextTransaction();
    CHECK(operation.type == ycsb::OperationType::Read);
    CHECK(keyId(operation.key) < config.record_count);
  }
}

TEST_CASE("YCSB core workload A stays close to a 50/50 read update mix") {
  ycsb::WorkloadConfig config;
  config.workload = ycsb::CoreWorkload::A;
  config.request_distribution = ycsb::RequestDistribution::Uniform;
  config.record_count = 1000;
  config.operation_count = 10000;
  config.seed = 7;

  ycsb::WorkloadGenerator generator(config);
  std::size_t reads = 0;
  std::size_t updates = 0;
  for (std::size_t i = 0; i < config.operation_count; ++i) {
    const auto operation = generator.nextTransaction();
    if (operation.type == ycsb::OperationType::Read) {
      ++reads;
    }
    if (operation.type == ycsb::OperationType::Update) {
      ++updates;
      CHECK(operation.value.size() == config.value_size);
    }
  }

  CHECK(reads + updates == config.operation_count);
  CHECK(reads > 4700);
  CHECK(reads < 5300);
}

TEST_CASE("YCSB core workload E emits scans and inserts") {
  ycsb::WorkloadConfig config;
  config.workload = ycsb::CoreWorkload::E;
  config.request_distribution = ycsb::RequestDistribution::Uniform;
  config.record_count = 10;
  config.operation_count = 2000;
  config.min_scan_length = 3;
  config.max_scan_length = 7;

  ycsb::WorkloadGenerator generator(config);
  std::size_t scans = 0;
  std::size_t inserts = 0;
  std::size_t largest_key_id = 0;
  for (std::size_t i = 0; i < config.operation_count; ++i) {
    const auto operation = generator.nextTransaction();
    largest_key_id = std::max(largest_key_id, keyId(operation.key));
    if (operation.type == ycsb::OperationType::Scan) {
      ++scans;
      CHECK(operation.scan_length >= config.min_scan_length);
      CHECK(operation.scan_length <= config.max_scan_length);
    }
    if (operation.type == ycsb::OperationType::Insert) {
      ++inserts;
    }
  }

  CHECK(scans > inserts);
  CHECK(inserts > 50);
  CHECK(largest_key_id >= config.record_count);
}

TEST_CASE("YCSB workload D biases reads toward latest records") {
  ycsb::WorkloadConfig config;
  config.workload = ycsb::CoreWorkload::D;
  config.record_count = 1000;
  config.operation_count = 1000;
  config.seed = 2;

  ycsb::WorkloadGenerator generator(config);
  std::size_t read_count = 0;
  std::size_t recent_reads = 0;
  for (std::size_t i = 0; i < config.operation_count; ++i) {
    const auto operation = generator.nextTransaction();
    if (operation.type == ycsb::OperationType::Read) {
      ++read_count;
      if (keyId(operation.key) >= config.record_count - 100) {
        ++recent_reads;
      }
    }
  }

  REQUIRE(read_count > 0);
  CHECK(recent_reads * 100 / read_count > 20);
}
