#include "key_transform.h"

#include <cstring>
#include <stdexcept>

namespace {

std::uint64_t read64(const char *data) {
  std::uint64_t value = 0;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

std::uint64_t multiplyMix(std::uint64_t lhs, std::uint64_t rhs) {
#if defined(__SIZEOF_INT128__)
  const auto product = static_cast<__uint128_t>(lhs) * static_cast<__uint128_t>(rhs);
  return static_cast<std::uint64_t>(product) ^ static_cast<std::uint64_t>(product >> 64);
#else
  lhs ^= lhs >> 33;
  lhs *= 0xff51afd7ed558ccdULL;
  lhs ^= lhs >> 33;
  lhs *= 0xc4ceb9fe1a85ec53ULL;
  lhs ^= lhs >> 33;
  return lhs ^ rhs;
#endif
}

std::uint64_t avalanche(std::uint64_t value) {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  value ^= value >> 31;
  return value;
}

std::uint32_t roundFunction(std::uint32_t half, std::uint64_t round_key) {
  std::uint64_t mixed = static_cast<std::uint64_t>(half) ^ round_key;
  mixed ^= mixed >> 23;
  mixed *= 0x2127599bf4325c37ULL;
  mixed ^= mixed >> 47;
  return static_cast<std::uint32_t>(mixed ^ (mixed >> 32));
}

void appendBigEndian64(std::string &out, std::uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    out.push_back(static_cast<char>((value >> shift) & 0xff));
  }
}

} // namespace

KeyTransform::KeyTransform(KeyTransformOptions options) : options_(options) {
  if (options_.block_size == 0) {
    throw std::invalid_argument("KeyTransform: block_size must be greater than zero");
  }
  if (options_.block_size > 1024) {
    throw std::invalid_argument("KeyTransform: block_size must be at most 1024");
  }

  round_keys_ = {
      options_.feistel_seed ^ 0xa0761d6478bd642fULL,
      options_.feistel_seed ^ 0xe7037ed1a0b428dbULL,
      options_.feistel_seed ^ 0x8ebc6af09c88c6e3ULL,
      options_.feistel_seed ^ 0x589965cc75374cc3ULL,
  };
}

std::string KeyTransform::storageKey(std::string_view key) const {
  const std::uint64_t normalized = normalize(key);
  if (!options_.shuffling_enabled) {
    return normalizedKey(normalized);
  }
  return shuffledKey(normalized);
}

bool KeyTransform::shufflingEnabled() const { return options_.shuffling_enabled; }

std::uint64_t KeyTransform::blockSize() const { return options_.block_size; }

std::uint64_t KeyTransform::normalize(std::string_view key) {
  const char *data = key.data();
  std::size_t remaining = key.size();
  std::uint64_t hash = 0xa0761d6478bd642fULL ^ static_cast<std::uint64_t>(remaining);

  while (remaining >= sizeof(std::uint64_t)) {
    const std::uint64_t word = read64(data);
    hash = multiplyMix(word ^ 0xe7037ed1a0b428dbULL, hash ^ 0x8ebc6af09c88c6e3ULL);
    data += sizeof(std::uint64_t);
    remaining -= sizeof(std::uint64_t);
  }

  std::uint64_t tail = 0;
  if (remaining > 0) {
    std::memcpy(&tail, data, remaining);
  }

  hash = multiplyMix(tail ^ 0x589965cc75374cc3ULL, hash ^ 0x1d8e4e27c47d124fULL);
  return avalanche(hash ^ static_cast<std::uint64_t>(key.size()));
}

std::string KeyTransform::shuffledKey(std::uint64_t normalized) const {
  const std::uint64_t permuted = feistel64(normalized);

  std::string out;
  out.reserve(9);
  out.push_back('s');
  appendBigEndian64(out, permuted);
  return out;
}

std::string KeyTransform::normalizedKey(std::uint64_t normalized) const {
  std::string out;
  out.reserve(9);
  out.push_back('h');
  appendBigEndian64(out, normalized);
  return out;
}

std::uint64_t KeyTransform::feistel64(std::uint64_t value) const {
  std::uint32_t left = static_cast<std::uint32_t>(value >> 32);
  std::uint32_t right = static_cast<std::uint32_t>(value);

  for (const auto round_key : round_keys_) {
    const std::uint32_t next_left = right;
    const std::uint32_t next_right = left ^ roundFunction(right, round_key);
    left = next_left;
    right = next_right;
  }

  return (static_cast<std::uint64_t>(left) << 32) | right;
}
