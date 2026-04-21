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

## Development Environment

- C++ standard: `C++17`
- Build system: `CMake`
- Test framework: `doctest v2.4.12`
- Main executable target: `cauli_base`
- Unit test target: `cauli_unit_tests`
- Benchmark target: `cauli_bench`

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

There is also a larger shuffling performance comparison test. It is gated behind an environment variable so normal test runs stay fast:

```bash
CAULI_RUN_SHUFFLE_PERF=1 ./build/test/cauli_unit_tests --test-case="performance / shuffling on vs off large dataset*"
```

The test loads 100,000 keys and prints `shuffle_on` versus `shuffle_off` timings for put/get/delete and total runtime.

## Benchmark

The benchmark program lives in `bench/` and measures the main database operations:

- `put`
- `get_memtable`
- `get_sstable`
- `del`
- `compact`

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

Example:

```bash
./build/bench/cauli_bench 1000 200 64 both
```

To exclude key normalization/shuffling from the measured operation time, precompute storage keys before each benchmark stage:

```bash
./build/bench/cauli_bench 10000 2000 64 both prepare-keys
```

This calls `prepareKeys(...)` before the timed `put/get/del/compact` sections, so the timed operations use cached internal keys.

For more stable results, pass a repeat count and compare the median plus min/max range:

```bash
./build/bench/cauli_bench 10000 2000 64 both prepare-keys 7
```

When `both` is selected, the benchmark includes `shuffle_vs_plain%`, which reports how much more time the shuffled run used compared with the matching non-shuffled run:

```text
benchmark                  ops       median_ms       avg_us/op         ops/sec        min_ms        max_ms shuffle_vs_plain%
----------------------------------------------------------------------------------------------------------------------------
put_shuffle               1000           9.850           9.850       101522.84         9.601        10.228             23.40
put_plain                 1000           7.982           7.982       125281.88         7.721         8.196                 -
```

In code, pass `KeyTransformOptions{true}` to enable shuffling or `KeyTransformOptions{false}` to keep only hash normalization:

```cpp
CauliBase shuffled(db_path, 1024, KeyTransformOptions{true});
CauliBase normalized_only(db_path, 1024, KeyTransformOptions{false});
```

## Commands

| Command | Description |
| --- | --- |
| `put <key> <value>` | Insert or update a key-value pair. The value may contain spaces. |
| `get <key>` | Print the value for a key, or a not-found message. |
| `del <key>` | Mark a key as deleted using a tombstone. |
| `flush` | Write the current memtable to a new SSTable and clear the WAL. |
| `compact` | Merge SSTables, keep the latest values, and remove tombstones. |
| `debug` | Print memtable size and SSTable paths. |
| `help` | Print available commands. |
| `exit` | Exit the CLI. |
