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

## Run

```bash
./build/src/cauli_base
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

Build and run:

```bash
cmake -S . -B build
cmake --build build
./build/bench/cauli_bench
```

Default benchmark settings:

```text
operations=10000
compact_operations=2000
value_size=64
```

You can override the settings:

```bash
./build/bench/cauli_bench [operations] [compact_operations] [value_size]
```

Example:

```bash
./build/bench/cauli_bench 1000 200 64
```

Sample output:

```text
CauliBase benchmark
operations=1000, compact_operations=200, value_size=64

benchmark                  ops        total_ms       avg_us/op         ops/sec
------------------------------------------------------------------------------
put                       1000           4.512           4.512       221649.63
get_memtable              1000           1.105           1.105       904635.99
get_sstable               1000         120.448         120.448         8302.36
del                       1000           2.269           2.269       440730.94
compact                    300          16.245          54.149        18467.55
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
