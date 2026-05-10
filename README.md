# CauliBase

CauliBase is a tiny key-value database prototype for research purpose. It uses a simple LSM-tree style design:

- writes first go to a WAL (Write-Ahead Log)
- recent data lives in an in-memory memtable
- flushed data is stored in immutable SSTable files
- compaction merges SSTables and removes deleted records

The project is intentionally compact and educational, with unit tests for the core storage components.

## Features

- `put <key> <value>`: insert or overwrite a key
- `get <key>`: read a key
- `del <key>`: logically delete a key with a tombstone
- `flush`: flush the current memtable to an SSTable
- `compact`: merge SSTables and discard tombstones
- `debug`: print current memtable and SSTable state
- WAL replay on startup for crash recovery of unflushed writes
- Key normalization with a short 64-bit hash-based internal key
- Optional Feistel-based pseudo-random permutation and 1000-slot block-level key shuffling
- SSTables include a Bloom filter and key-offset index for faster point lookups

## Development Environment

- C++ standard: `C++17`
- Build system: `CMake`
- Test framework: `doctest v2.4.12`
- Main executable target: `cauli_base`
- Unit test target: `cauli_unit_tests`
- Benchmark target: `cauli_bench`
- YCSB workload generator target: `ycsb_workload_generator`
- YCSB benchmark target: `ycsb_bench`

## Build

```bash
cmake -S . -B build
cmake --build build
```

By default, the CLI stores database files in:

```text
data/
  wal.log
  000001.sst
  000002.sst
  ...
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

You can also run the doctest binary directly:

```bash
./build/test/cauli_unit_tests
```

## Benchmark

The benchmark program lives in `bench/` and measures the main database operations:

- `put`
- `get_memtable`
- `get_sstable`
- `del`
- `compact`

SSTable point lookups use a Bloom filter to reject absent keys, then binary-search a key-offset index and seek directly to the matching record. Older SSTable files without metadata still fall back to sequential scanning.

Default benchmark settings:

```text
operations=10000
compact_operations=2000
value_size=64
```

You can override the settings:

```bash
./build/bench/cauli_bench [operations] [compact_operations] [value_size] [both|shuffle-on|shuffle-off] [prepare-keys|no-prepare] [repeats]
```

## YCSB Workload Generator

The benchmark directory also includes a YCSB-style workload generator. It emits a load phase of `INSERT` records followed by a transaction phase matching the standard core workloads:

- `a`: 50% reads, 50% updates
- `b`: 95% reads, 5% updates
- `c`: 100% reads
- `d`: 95% latest-biased reads, 5% inserts
- `e`: 95% scans, 5% inserts
- `f`: 50% reads, 50% read-modify-writes

```bash
./build/bench/ycsb_workload_generator [a|b|c|d|e|f] [record_count] [operation_count] [value_size] [uniform|zipfian|latest] [seed]
```

To execute those workloads against CauliBase and report load/transaction throughput:

```bash
./build/bench/ycsb_bench [all|a|b|c|d|e|f] [record_count] [operation_count] [value_size] [uniform|zipfian|latest] [shuffle-on|shuffle-off] [seed] [memtable_limit]
```

`all` runs workloads A, B, C, and F.
