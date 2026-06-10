# forest-sorting

Sorting a forest of nodes deterministically by depth, then by node ID with an
adaptive word-first radix sort.

The production sorter is tuned for common depths `0-30`, supports outliers up
to `kMaxSortableDepth` (`1024`), and rejects deeper forests. IDs are sorted by
most-significant 64-bit words first, so random hash-like IDs usually only need
their high word inspected; lower words are used only inside equal-prefix
ranges. Duplicate full IDs are rejected.

## Building

This repository uses CMake and a C++20 `clang++` available on `PATH`.

```bash
cmake --preset release            # generate Release build files into out/build/release
cmake --build --preset release    # compile optimized binaries
./out/build/release/forest-sorting
```

On macOS with Homebrew LLVM, put LLVM first on `PATH` before configuring:

```bash
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
```

## Tests

```bash
cmake --preset debug              # generate Debug build files with ASan/UBSan enabled
cmake --build --preset debug      # compile with sanitizers
ctest --preset debug              # run the test suite with sanitizers active
```

The regression tests check deterministic ordering across multiple input
permutations, verify the production adaptive radix sort against comparison and
full-LSD radix baselines, and cover duplicate full-ID rejection.

## Linting

If `clang-tidy` is available on `PATH`, CMake adds a `tidy` target:

```bash
cmake --preset debug
cmake --build --preset debug --target tidy
```

To use a specific `clang-tidy`, configure with
`-DFOREST_CLANG_TIDY=/path/to/clang-tidy`.

## Benchmarks

The release build includes a small deterministic benchmark comparing
comparison sort, bucketed full-LSD radix sort, composite full-LSD radix sort,
and the production adaptive word-first radix sort:

```bash
cmake --build --preset release --target forest-sorting-bench
./out/build/release/forest-sorting-bench
```

CI runs on GitHub Actions for macOS and Ubuntu using these presets. It builds
and tests Debug with ASan/UBSan, runs `clang-tidy`, builds Release, smoke-runs
the benchmark executable, and runs the Release tests.
