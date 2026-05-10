#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace ycsb {

enum class OperationType {
  Read,
  Update,
  Insert,
  Scan,
  ReadModifyWrite,
};

enum class CoreWorkload {
  A,
  B,
  C,
  D,
  E,
  F,
};

enum class RequestDistribution {
  Uniform,
  Zipfian,
  Latest,
};

struct Operation {
  OperationType type = OperationType::Read;
  std::string key;
  std::string value;
  std::size_t scan_length = 0;
};

struct WorkloadConfig {
  CoreWorkload workload = CoreWorkload::A;
  RequestDistribution request_distribution = RequestDistribution::Zipfian;
  std::size_t record_count = 10000;
  std::size_t operation_count = 10000;
  std::size_t value_size = 100;
  std::size_t min_scan_length = 1;
  std::size_t max_scan_length = 100;
  double zipfian_theta = 0.99;
  std::uint64_t seed = 1;
};

struct OperationMix {
  double read = 0.0;
  double update = 0.0;
  double insert = 0.0;
  double scan = 0.0;
  double read_modify_write = 0.0;
};

const char *operationName(OperationType type);
const char *workloadName(CoreWorkload workload);

CoreWorkload parseCoreWorkload(const std::string &value);
RequestDistribution parseRequestDistribution(const std::string &value);

class WorkloadGenerator {
public:
  explicit WorkloadGenerator(WorkloadConfig config);

  std::vector<Operation> loadOperations() const;
  Operation nextTransaction();

  const WorkloadConfig &config() const { return config_; }
  std::size_t transactionIndex() const { return transaction_index_; }

  static std::string keyFor(std::size_t id);
  static std::string valueFor(std::size_t id, std::size_t value_size);

private:
  OperationType chooseOperation();
  std::size_t chooseExistingKey();
  std::size_t chooseUniformKey();
  std::size_t chooseZipfianKey();
  std::size_t chooseLatestKey();
  std::size_t chooseScanLength();
  std::size_t nextInsertKey();
  void rebuildZipfianCdf();

  WorkloadConfig config_;
  OperationMix mix_;
  std::mt19937_64 rng_;
  std::uniform_real_distribution<double> unit_;
  std::vector<double> zipfian_cdf_;
  std::size_t current_record_count_ = 0;
  std::size_t next_insert_id_ = 0;
  std::size_t transaction_index_ = 0;
};

} // namespace ycsb
