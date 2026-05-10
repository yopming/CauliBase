#include "ycsb_workload.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace ycsb {
namespace {

OperationMix mixFor(CoreWorkload workload) {
  switch (workload) {
  case CoreWorkload::A:
    return OperationMix{0.50, 0.50, 0.0, 0.0, 0.0};
  case CoreWorkload::B:
    return OperationMix{0.95, 0.05, 0.0, 0.0, 0.0};
  case CoreWorkload::C:
    return OperationMix{1.00, 0.0, 0.0, 0.0, 0.0};
  case CoreWorkload::D:
    return OperationMix{0.95, 0.0, 0.05, 0.0, 0.0};
  case CoreWorkload::E:
    return OperationMix{0.0, 0.0, 0.05, 0.95, 0.0};
  case CoreWorkload::F:
    return OperationMix{0.50, 0.0, 0.0, 0.0, 0.50};
  }
  throw std::invalid_argument("Unknown YCSB workload.");
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

} // namespace

const char *operationName(OperationType type) {
  switch (type) {
  case OperationType::Read:
    return "READ";
  case OperationType::Update:
    return "UPDATE";
  case OperationType::Insert:
    return "INSERT";
  case OperationType::Scan:
    return "SCAN";
  case OperationType::ReadModifyWrite:
    return "READMODIFYWRITE";
  }
  return "UNKNOWN";
}

const char *workloadName(CoreWorkload workload) {
  switch (workload) {
  case CoreWorkload::A:
    return "workloada";
  case CoreWorkload::B:
    return "workloadb";
  case CoreWorkload::C:
    return "workloadc";
  case CoreWorkload::D:
    return "workloadd";
  case CoreWorkload::E:
    return "workloade";
  case CoreWorkload::F:
    return "workloadf";
  }
  return "unknown";
}

CoreWorkload parseCoreWorkload(const std::string &value) {
  const std::string normalized = lowercase(value);
  if (normalized == "a" || normalized == "workloada") {
    return CoreWorkload::A;
  }
  if (normalized == "b" || normalized == "workloadb") {
    return CoreWorkload::B;
  }
  if (normalized == "c" || normalized == "workloadc") {
    return CoreWorkload::C;
  }
  if (normalized == "d" || normalized == "workloadd") {
    return CoreWorkload::D;
  }
  if (normalized == "e" || normalized == "workloade") {
    return CoreWorkload::E;
  }
  if (normalized == "f" || normalized == "workloadf") {
    return CoreWorkload::F;
  }
  throw std::invalid_argument("Invalid YCSB workload: " + value);
}

RequestDistribution parseRequestDistribution(const std::string &value) {
  const std::string normalized = lowercase(value);
  if (normalized == "uniform") {
    return RequestDistribution::Uniform;
  }
  if (normalized == "zipfian") {
    return RequestDistribution::Zipfian;
  }
  if (normalized == "latest") {
    return RequestDistribution::Latest;
  }
  throw std::invalid_argument("Invalid request distribution: " + value);
}

WorkloadGenerator::WorkloadGenerator(WorkloadConfig config)
    : config_(config), mix_(mixFor(config.workload)), rng_(config.seed), unit_(0.0, 1.0),
      current_record_count_(config.record_count), next_insert_id_(config.record_count) {
  if (config_.record_count == 0) {
    throw std::invalid_argument("YCSB record_count must be greater than zero.");
  }
  if (config_.max_scan_length < config_.min_scan_length || config_.min_scan_length == 0) {
    throw std::invalid_argument("YCSB scan length bounds must be positive and ordered.");
  }
  if (config_.zipfian_theta < 0.0) {
    throw std::invalid_argument("YCSB zipfian_theta must be non-negative.");
  }
  if (config_.workload == CoreWorkload::D) {
    config_.request_distribution = RequestDistribution::Latest;
  }
  if (config_.request_distribution == RequestDistribution::Zipfian) {
    rebuildZipfianCdf();
  }
}

std::vector<Operation> WorkloadGenerator::loadOperations() const {
  std::vector<Operation> operations;
  operations.reserve(config_.record_count);
  for (std::size_t i = 0; i < config_.record_count; ++i) {
    operations.push_back(Operation{OperationType::Insert, keyFor(i), valueFor(i, config_.value_size), 0});
  }
  return operations;
}

Operation WorkloadGenerator::nextTransaction() {
  const OperationType type = chooseOperation();
  Operation operation;
  operation.type = type;

  if (type == OperationType::Insert) {
    const std::size_t key_id = nextInsertKey();
    operation.key = keyFor(key_id);
    operation.value = valueFor(key_id, config_.value_size);
  } else {
    const std::size_t key_id = chooseExistingKey();
    operation.key = keyFor(key_id);
    if (type == OperationType::Update || type == OperationType::ReadModifyWrite) {
      operation.value = valueFor(transaction_index_ + config_.record_count, config_.value_size);
    }
    if (type == OperationType::Scan) {
      operation.scan_length = chooseScanLength();
    }
  }

  ++transaction_index_;
  return operation;
}

std::string WorkloadGenerator::keyFor(std::size_t id) {
  std::ostringstream out;
  out << "user" << std::setw(20) << std::setfill('0') << id;
  return out.str();
}

std::string WorkloadGenerator::valueFor(std::size_t id, std::size_t value_size) {
  std::string value = "field0=value" + std::to_string(id);
  if (value.size() < value_size) {
    value.append(value_size - value.size(), 'x');
  }
  if (value.size() > value_size) {
    value.resize(value_size);
  }
  return value;
}

OperationType WorkloadGenerator::chooseOperation() {
  const double roll = unit_(rng_);
  double cumulative = mix_.read;
  if (roll < cumulative) {
    return OperationType::Read;
  }
  cumulative += mix_.update;
  if (roll < cumulative) {
    return OperationType::Update;
  }
  cumulative += mix_.insert;
  if (roll < cumulative) {
    return OperationType::Insert;
  }
  cumulative += mix_.scan;
  if (roll < cumulative) {
    return OperationType::Scan;
  }
  return OperationType::ReadModifyWrite;
}

std::size_t WorkloadGenerator::chooseExistingKey() {
  switch (config_.request_distribution) {
  case RequestDistribution::Uniform:
    return chooseUniformKey();
  case RequestDistribution::Zipfian:
    return chooseZipfianKey();
  case RequestDistribution::Latest:
    return chooseLatestKey();
  }
  return chooseUniformKey();
}

std::size_t WorkloadGenerator::chooseUniformKey() {
  std::uniform_int_distribution<std::size_t> distribution(0, current_record_count_ - 1);
  return distribution(rng_);
}

std::size_t WorkloadGenerator::chooseZipfianKey() {
  const auto it = std::lower_bound(zipfian_cdf_.begin(), zipfian_cdf_.end(), unit_(rng_));
  if (it == zipfian_cdf_.end()) {
    return current_record_count_ - 1;
  }
  return static_cast<std::size_t>(std::distance(zipfian_cdf_.begin(), it));
}

std::size_t WorkloadGenerator::chooseLatestKey() {
  if (current_record_count_ == 1) {
    return 0;
  }

  const double roll = std::max(unit_(rng_), std::numeric_limits<double>::min());
  const double skewed = std::pow(roll, 8.0);
  const auto offset = static_cast<std::size_t>(skewed * static_cast<double>(current_record_count_));
  return current_record_count_ - 1 - std::min(offset, current_record_count_ - 1);
}

std::size_t WorkloadGenerator::chooseScanLength() {
  std::uniform_int_distribution<std::size_t> distribution(config_.min_scan_length, config_.max_scan_length);
  return distribution(rng_);
}

std::size_t WorkloadGenerator::nextInsertKey() {
  const std::size_t key_id = next_insert_id_++;
  ++current_record_count_;
  if (config_.request_distribution == RequestDistribution::Zipfian) {
    rebuildZipfianCdf();
  }
  return key_id;
}

void WorkloadGenerator::rebuildZipfianCdf() {
  zipfian_cdf_.clear();
  zipfian_cdf_.reserve(current_record_count_);

  double normalization = 0.0;
  for (std::size_t i = 1; i <= current_record_count_; ++i) {
    normalization += 1.0 / std::pow(static_cast<double>(i), config_.zipfian_theta);
  }

  double cumulative = 0.0;
  for (std::size_t i = 1; i <= current_record_count_; ++i) {
    cumulative += (1.0 / std::pow(static_cast<double>(i), config_.zipfian_theta)) / normalization;
    zipfian_cdf_.push_back(cumulative);
  }
  zipfian_cdf_.back() = 1.0;
}

} // namespace ycsb
