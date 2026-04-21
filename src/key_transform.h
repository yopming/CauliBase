#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

struct KeyTransformOptions {
  bool shuffling_enabled = true;
  std::uint64_t feistel_seed = 0x9e3779b97f4a7c15ULL;
  std::uint64_t block_size = 1000;
};

// Converts external user keys into short internal storage keys.
// With shuffling enabled, the Feistel PRP position maps to block=position/1000 and slot=position%1000.
class KeyTransform {
public:
  explicit KeyTransform(KeyTransformOptions options = {});

  std::string storageKey(std::string_view key) const;

  bool shufflingEnabled() const;
  std::uint64_t blockSize() const;

  static std::uint64_t normalize(std::string_view key);

private:
  std::string shuffledKey(std::uint64_t normalized) const;
  std::string normalizedKey(std::uint64_t normalized) const;

  std::uint64_t feistel64(std::uint64_t value) const;

  KeyTransformOptions options_;
  std::array<std::uint64_t, 4> round_keys_;
};
