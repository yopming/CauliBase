#include <doctest/doctest.h>

#include "cauli_base.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using Clock = std::chrono::steady_clock;

class PerfTempDir {
public:
  explicit PerfTempDir(std::string_view name) {
    const auto now = Clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("cauli_shuffle_perf_" + std::string(name) + "_" + std::to_string(now));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }

  ~PerfTempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

struct StageTimer {
  Clock::time_point started = Clock::now();

  double elapsedMs() const {
    const auto ended = Clock::now();
    return std::chrono::duration<double, std::milli>(ended - started).count();
  }
};

struct PerfResult {
  std::string name;
  std::size_t operations = 0;
  double put_ms = 0.0;
  double get_ms = 0.0;
  double delete_ms = 0.0;

  double totalMs() const { return put_ms + get_ms + delete_ms; }
};

std::string perfKey(std::size_t i) { return "perf_key_" + std::to_string(i); }

std::string perfValue(std::size_t i) {
  std::string value = "perf_value_" + std::to_string(i) + "_";
  value.append(128 - std::min<std::size_t>(value.size(), 128), 'x');
  return value;
}

PerfResult runShufflePerf(std::string name, bool shuffling_enabled, std::size_t operations) {
  PerfTempDir temp(name);
  CauliBase db(temp.path(), operations + 1, KeyTransformOptions{shuffling_enabled});

  StageTimer put_timer;
  for (std::size_t i = 0; i < operations; ++i) {
    db.put(perfKey(i), perfValue(i));
  }
  const double put_ms = put_timer.elapsedMs();

  StageTimer get_timer;
  std::size_t found = 0;
  for (std::size_t i = operations; i > 0; --i) {
    const std::optional<std::string> value = db.get(perfKey(i - 1));
    if (value.has_value()) {
      ++found;
    }
  }
  const double get_ms = get_timer.elapsedMs();
  REQUIRE(found == operations);

  StageTimer delete_timer;
  for (std::size_t i = 0; i < operations; i += 2) {
    db.del(perfKey(i));
  }
  const double delete_ms = delete_timer.elapsedMs();

  for (std::size_t i = 0; i < operations; ++i) {
    const bool should_exist = (i % 2) != 0;
    CHECK(db.get(perfKey(i)).has_value() == should_exist);
  }

  return PerfResult{std::move(name), operations, put_ms, get_ms, delete_ms};
}

void printPerfResult(const PerfResult &result) {
  std::cout << std::left << std::setw(16) << result.name << std::right << std::setw(12) << result.operations
            << std::setw(14) << std::fixed << std::setprecision(3) << result.put_ms << std::setw(14)
            << result.get_ms << std::setw(14) << result.delete_ms << std::setw(14) << result.totalMs() << "\n";
}

} // namespace

TEST_CASE("performance / shuffling on vs off large dataset [.][performance]") {
  const char *run_perf = std::getenv("CAULI_RUN_SHUFFLE_PERF");
  if (run_perf == nullptr || std::string_view(run_perf) != "1") {
    return;
  }

  constexpr std::size_t operations = 100000;

  const PerfResult shuffled = runShufflePerf("shuffle_on", true, operations);
  const PerfResult plain = runShufflePerf("shuffle_off", false, operations);

  std::cout << "\nShuffling performance comparison\n";
  std::cout << std::left << std::setw(16) << "mode" << std::right << std::setw(12) << "ops" << std::setw(14)
            << "put_ms" << std::setw(14) << "get_ms" << std::setw(14) << "del_ms" << std::setw(14) << "total_ms"
            << "\n";
  std::cout << std::string(84, '-') << "\n";
  printPerfResult(shuffled);
  printPerfResult(plain);

  const double ratio = shuffled.totalMs() / plain.totalMs();
  std::cout << "shuffle/plain total ratio: " << std::fixed << std::setprecision(3) << ratio << "\n";

  CHECK(shuffled.operations == operations);
  CHECK(plain.operations == operations);
}
