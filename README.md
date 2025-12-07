# forest-sorting

Sorting a forest of nodes deterministically

## Building

This repository uses CMake and a C++20 compiler.

```bash
cmake --preset release            # generate Release build files into out/build/release
cmake --build --preset release    # compile optimized binaries
./out/build/release/forest-sorting
```

## Tests

```bash
cmake --preset debug              # generate Debug build files with ASan/UBSan enabled
cmake --build --preset debug      # compile with sanitizers
ctest --preset debug              # run the test suite with sanitizers active
```

CI runs on GitHub Actions for macOS and Ubuntu using these presets; Debug uses ASan/UBSan, and Release builds are also compiled and tested.
