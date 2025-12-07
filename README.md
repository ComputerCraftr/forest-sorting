# forest-sorting
Sorting a forest of nodes deterministically

## Building

This repository uses CMake and a C++20 compiler.

```bash
cmake --preset release
cmake --build --preset release
./out/build/release/forest-sorting
```

## Tests

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

CI runs on GitHub Actions for macOS and Ubuntu using these presets; Debug uses ASan/UBSan, and Release builds are also compiled and tested.
```
