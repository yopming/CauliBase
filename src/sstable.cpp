#include "sstable.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <ios>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint64_t kFooterMagic = 0x4341554c49535354ULL; // "CAULISST"
constexpr std::uint64_t kBloomBitsPerRecord = 10;
constexpr std::uint32_t kBloomHashCount = 7;

struct Footer {
  std::uint64_t magic = kFooterMagic;
  std::uint64_t index_offset = 0;
  std::uint64_t index_count = 0;
  std::uint64_t bloom_offset = 0;
  std::uint64_t bloom_bit_count = 0;
  std::uint32_t bloom_hash_count = 0;
};

std::uint64_t fnv1a(std::string_view key, std::uint64_t seed) {
  std::uint64_t hash = 1469598103934665603ULL ^ seed;
  for (unsigned char ch : key) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  hash ^= hash >> 32;
  return hash;
}

std::uint64_t bloomBitCount(std::uint64_t record_count) {
  if (record_count == 0) {
    return 64;
  }
  return std::max<std::uint64_t>(64, record_count * kBloomBitsPerRecord);
}

std::vector<std::uint8_t> makeBloom(std::uint64_t bit_count) {
  return std::vector<std::uint8_t>((bit_count + 7) / 8, 0);
}

void setBloomBit(std::vector<std::uint8_t> &bits, std::uint64_t bit) {
  bits[bit / 8] = static_cast<std::uint8_t>(bits[bit / 8] | (1U << (bit % 8)));
}

bool getBloomBit(const std::vector<std::uint8_t> &bits, std::uint64_t bit) {
  return (bits[bit / 8] & (1U << (bit % 8))) != 0;
}

void addToBloom(std::vector<std::uint8_t> &bits, std::uint64_t bit_count, const std::string &key) {
  const std::uint64_t first = fnv1a(key, 0xa0761d6478bd642fULL);
  const std::uint64_t second = fnv1a(key, 0xe7037ed1a0b428dbULL) | 1ULL;
  for (std::uint32_t i = 0; i < kBloomHashCount; ++i) {
    setBloomBit(bits, (first + i * second) % bit_count);
  }
}

} // namespace

/// Initializer for one sstable
SSTable::SSTable(std::filesystem::path path) : sstable_path_(path) {}

/// Getter for the system path of sst file. Structure of data is the same as in WAL
const std::filesystem::path &SSTable::path() const { return sstable_path_; }

void SSTable::writeFromMap(const std::map<std::string, Record> &memtable) {
  // truncate if file exists
  std::ofstream out(sstable_path_, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) throw std::runtime_error("SSTable: failed to create sstable file: " + sstable_path_.string());

  // number of records in memtable, and write it to SStable header
  uint64_t record_count = static_cast<uint64_t>(memtable.size());
  out.write(reinterpret_cast<const char *>(&record_count), sizeof(record_count));

  std::vector<IndexEntry> index;
  index.reserve(memtable.size());
  const std::uint64_t bit_count = bloomBitCount(record_count);
  std::vector<std::uint8_t> bloom = makeBloom(bit_count);

  // visit every record in memtable. Since memtable is ordered, written sst is also sorted by key
  for (const auto &[key, record] : memtable) {
    (void)key; // record.key has same information

    const auto record_offset = static_cast<std::uint64_t>(out.tellp());
    index.push_back(IndexEntry{record.key, record_offset});
    addToBloom(bloom, bit_count, record.key);

    uint32_t key_size = static_cast<uint32_t>(record.key.size());
    uint32_t val_size = static_cast<uint32_t>(record.val.size());
    uint8_t tombstone = record.tombstone ? 1 : 0; // true -> 1, false -> 0

    // write to sst
    out.write(reinterpret_cast<const char *>(&key_size), sizeof(key_size));
    out.write(reinterpret_cast<const char *>(&val_size), sizeof(val_size));
    out.write(reinterpret_cast<const char *>(&tombstone), sizeof(tombstone));

    if (key_size > 0) {
      out.write(record.key.data(), static_cast<std::streamsize>(record.key.size()));
    }
    if (val_size > 0) {
      out.write(record.val.data(), static_cast<std::streamsize>(record.val.size()));
    }

    if (!out) throw std::runtime_error("SSTable: failed to write.");
  }

  const auto index_offset = static_cast<std::uint64_t>(out.tellp());
  const auto index_count = static_cast<std::uint64_t>(index.size());
  out.write(reinterpret_cast<const char *>(&index_count), sizeof(index_count));
  for (const auto &entry : index) {
    const auto key_size = static_cast<std::uint32_t>(entry.key.size());
    out.write(reinterpret_cast<const char *>(&key_size), sizeof(key_size));
    out.write(entry.key.data(), static_cast<std::streamsize>(entry.key.size()));
    out.write(reinterpret_cast<const char *>(&entry.offset), sizeof(entry.offset));
  }

  const auto bloom_offset = static_cast<std::uint64_t>(out.tellp());
  const auto bloom_byte_count = static_cast<std::uint64_t>(bloom.size());
  const auto bloom_hash_count = kBloomHashCount;
  out.write(reinterpret_cast<const char *>(&bit_count), sizeof(bit_count));
  out.write(reinterpret_cast<const char *>(&bloom_hash_count), sizeof(bloom_hash_count));
  out.write(reinterpret_cast<const char *>(&bloom_byte_count), sizeof(bloom_byte_count));
  if (!bloom.empty()) {
    out.write(reinterpret_cast<const char *>(bloom.data()), static_cast<std::streamsize>(bloom.size()));
  }

  const Footer footer{kFooterMagic, index_offset, index_count, bloom_offset, bit_count, bloom_hash_count};
  out.write(reinterpret_cast<const char *>(&footer), sizeof(footer));
  if (!out) throw std::runtime_error("SSTable: failed to write metadata.");

  metadata_cache_ = Metadata{std::move(index), std::move(bloom), bit_count, kBloomHashCount};
}

std::optional<Record> SSTable::get(const std::string &target_key) const {
  std::ifstream in(sstable_path_, std::ios::binary);
  if (!in) throw std::runtime_error("SSTable: failed to open the sstable file " + sstable_path_.string());

  const auto &loaded_metadata = metadata();
  if (loaded_metadata.has_value()) {
    const Metadata &meta = *loaded_metadata;
    if (!bloomMayContain(meta, target_key)) {
      return std::nullopt;
    }

    const auto found = std::lower_bound(meta.index.begin(), meta.index.end(), target_key,
                                        [](const IndexEntry &entry, const std::string &key) {
                                          return entry.key < key;
                                        });
    if (found == meta.index.end() || found->key != target_key) {
      return std::nullopt;
    }

    in.seekg(static_cast<std::streamoff>(found->offset), std::ios::beg);
    if (!in) throw std::runtime_error("SSTable: failed to seek to indexed record.");
    return readRecord(in);
  }

  // read number of records in sst
  uint64_t record_count = 0;
  in.read(reinterpret_cast<char *>(&record_count), sizeof(record_count));
  if (!in) throw std::runtime_error("SSTable: corrupted sstable file header in " + sstable_path_.string());

  // check all records in SStable one by one
  for (uint64_t i = 0; i < record_count; ++i) {
    Record record = readRecord(in);
    if (record.key == target_key) return record;

    // if current record.key is larger than target, no need to continue (since sorted)
    if (record.key > target_key) break;
  }

  return std::nullopt;
}

std::map<std::string, Record> SSTable::loadAll() const {
  std::ifstream in(sstable_path_, std::ios::binary);
  if (!in) throw std::runtime_error("SSTable: failed to open sstable file " + sstable_path_.string());

  // get number of records in sst
  uint64_t record_count = 0;
  in.read(reinterpret_cast<char *>(&record_count), sizeof(record_count));
  if (!in) throw std::runtime_error("SSTable: corrupted sstable file header in " + sstable_path_.string());

  std::map<std::string, Record> results;
  for (uint64_t i = 0; i < record_count; ++i) {
    Record record = readRecord(in);
    results[record.key] = std::move(record);
  }

  return results;
}

/// PRIVATE

/**
 * @brief Read one record from ifstream, the read pointer will be updated automatically
 *
 * @param in ifstream can maintain a read pointer
 * @return Record
 */
Record SSTable::readRecord(std::ifstream &in) const {
  uint32_t key_size = 0;
  uint32_t val_size = 0;
  uint8_t tombstone = 0;

  in.read(reinterpret_cast<char *>(&key_size), sizeof(key_size));
  in.read(reinterpret_cast<char *>(&val_size), sizeof(val_size));
  in.read(reinterpret_cast<char *>(&tombstone), sizeof(tombstone));
  if (!in) throw std::runtime_error("SSTable: corrupted sstable header.");

  Record record;
  record.key.resize(key_size);
  record.val.resize(val_size);
  record.tombstone = (tombstone != 0);

  in.read(record.key.data(), static_cast<std::streamsize>(key_size));
  in.read(record.val.data(), static_cast<std::streamsize>(val_size));
  if (!in) throw std::runtime_error("SSTable: corrupted record body.");

  return record;
}

std::optional<SSTable::Metadata> SSTable::loadMetadata() const {
  std::ifstream in(sstable_path_, std::ios::binary);
  if (!in) throw std::runtime_error("SSTable: failed to open sstable file " + sstable_path_.string());

  in.seekg(0, std::ios::end);
  const auto file_size_pos = in.tellg();
  if (file_size_pos < static_cast<std::streamoff>(sizeof(Footer))) {
    return std::nullopt;
  }

  in.seekg(-static_cast<std::streamoff>(sizeof(Footer)), std::ios::end);
  Footer footer;
  in.read(reinterpret_cast<char *>(&footer), sizeof(footer));
  if (!in) throw std::runtime_error("SSTable: corrupted metadata footer.");
  if (footer.magic != kFooterMagic) {
    return std::nullopt;
  }

  const auto file_size = static_cast<std::uint64_t>(file_size_pos);
  if (footer.index_offset >= file_size || footer.bloom_offset >= file_size ||
      footer.bloom_offset < footer.index_offset || footer.bloom_hash_count == 0 || footer.bloom_bit_count == 0) {
    throw std::runtime_error("SSTable: invalid metadata footer.");
  }

  Metadata metadata;
  metadata.index.reserve(static_cast<std::size_t>(footer.index_count));
  metadata.bloom_bit_count = footer.bloom_bit_count;
  metadata.bloom_hash_count = footer.bloom_hash_count;

  in.seekg(static_cast<std::streamoff>(footer.index_offset), std::ios::beg);
  std::uint64_t index_count = 0;
  in.read(reinterpret_cast<char *>(&index_count), sizeof(index_count));
  if (!in || index_count != footer.index_count) {
    throw std::runtime_error("SSTable: corrupted index block.");
  }

  for (std::uint64_t i = 0; i < index_count; ++i) {
    std::uint32_t key_size = 0;
    in.read(reinterpret_cast<char *>(&key_size), sizeof(key_size));
    if (!in) throw std::runtime_error("SSTable: corrupted index key header.");

    IndexEntry entry;
    entry.key.resize(key_size);
    if (key_size > 0) {
      in.read(entry.key.data(), static_cast<std::streamsize>(key_size));
    }
    in.read(reinterpret_cast<char *>(&entry.offset), sizeof(entry.offset));
    if (!in) throw std::runtime_error("SSTable: corrupted index entry.");
    metadata.index.push_back(std::move(entry));
  }

  in.seekg(static_cast<std::streamoff>(footer.bloom_offset), std::ios::beg);
  std::uint64_t bloom_bit_count = 0;
  std::uint32_t bloom_hash_count = 0;
  std::uint64_t bloom_byte_count = 0;
  in.read(reinterpret_cast<char *>(&bloom_bit_count), sizeof(bloom_bit_count));
  in.read(reinterpret_cast<char *>(&bloom_hash_count), sizeof(bloom_hash_count));
  in.read(reinterpret_cast<char *>(&bloom_byte_count), sizeof(bloom_byte_count));
  if (!in || bloom_bit_count != footer.bloom_bit_count || bloom_hash_count != footer.bloom_hash_count ||
      bloom_byte_count != (bloom_bit_count + 7) / 8) {
    throw std::runtime_error("SSTable: corrupted bloom filter.");
  }

  metadata.bloom_bits.resize(static_cast<std::size_t>(bloom_byte_count));
  if (bloom_byte_count > 0) {
    in.read(reinterpret_cast<char *>(metadata.bloom_bits.data()), static_cast<std::streamsize>(bloom_byte_count));
  }
  if (!in) throw std::runtime_error("SSTable: corrupted bloom filter body.");

  return metadata;
}

const std::optional<SSTable::Metadata> &SSTable::metadata() const {
  if (!metadata_cache_.has_value()) {
    metadata_cache_ = loadMetadata();
  }
  return *metadata_cache_;
}

bool SSTable::bloomMayContain(const Metadata &metadata, const std::string &key) const {
  if (metadata.bloom_bits.empty() || metadata.bloom_bit_count == 0 || metadata.bloom_hash_count == 0) {
    return true;
  }

  const std::uint64_t first = fnv1a(key, 0xa0761d6478bd642fULL);
  const std::uint64_t second = fnv1a(key, 0xe7037ed1a0b428dbULL) | 1ULL;
  for (std::uint32_t i = 0; i < metadata.bloom_hash_count; ++i) {
    const std::uint64_t bit = (first + i * second) % metadata.bloom_bit_count;
    if (!getBloomBit(metadata.bloom_bits, bit)) {
      return false;
    }
  }
  return true;
}
