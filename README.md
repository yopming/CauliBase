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


By default, the CLI stores database files in:

```text
data/
  wal.log
  000001.sst
  000002.sst
  ...
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
