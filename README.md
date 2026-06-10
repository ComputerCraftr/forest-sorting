# forest-sorting

Header-only C++20 library for sorting a forest of nodes deterministically by
depth, then by node ID with an adaptive byte-width radix sort.

The production sorter is tuned for common depths `0-30`, supports outliers up
to the selected compile-time depth prefix, and rejects deeper forests. IDs are
exposed through fixed-width most-significant-first bytes; the implementation can
chunk those bytes internally for speed. Duplicate full IDs are rejected.

Parent ID lookup uses a control-byte open-addressed ID-to-index hash table,
then the rest of the sort and verifier operate on integer-indexed vectors.

## API

The portable generic algorithm header does not depend on `unsigned __int128`:

```cpp
#include <forest_sorting/algorithms.hpp>
```

Portable users provide their own node and ID representation, plus a trait that
exposes ID bytes, hashing, equality, parent lookup, and root-parent semantics.
The primary primitive returns a non-owning sorted index order:

```cpp
auto order = forest_sorting::sortedOrderByDepthAndId<2>(
    nodes, MyCombinedTraits{});
```

The template argument is the depth-prefix byte count. The default overload uses
`2`, which supports depths up to `65535`.

Convenience wrappers for sorted copy, in-place sorting, and verification are
thin layers over the same order primitive.

The current `unsigned __int128` node API is available only from guarded
optional headers on compilers that support `__SIZEOF_INT128__`:

```cpp
#include <forest_sorting/uint128_forest.hpp>
```

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
full-LSD radix baselines, compare parent-index builders, compile the portable
algorithm header without UInt128 compatibility headers, and cover duplicate
full-ID rejection.

## Linting

If `clang-tidy` is available on `PATH`, CMake adds a `tidy` target:

```bash
cmake --preset debug
cmake --build --preset debug --target tidy
```

To use a specific `clang-tidy`, configure with
`-DFOREST_CLANG_TIDY=/path/to/clang-tidy`.

## Benchmarks

The release build includes a deterministic benchmark matrix comparing parent
index construction with `std::unordered_map`, the original flat hash,
production control-byte flat hash, and radix join baselines. It can also time
comparison, bucketed LSD radix, composite LSD radix, and adaptive MSD radix
sorts with selectable datasets and output formats:

```bash
cmake --build --preset release --target forest-sorting-bench
./out/build/release/forest-sorting-bench
./out/build/release/forest-sorting-bench --format csv --size 10000 --dataset random --parent all --sort adaptive-msd
./out/build/release/forest-sorting-bench --format json --size 10000 --dataset same-high64 --parent all --sort adaptive-msd
```

CI runs on GitHub Actions for macOS and Ubuntu using these presets. It builds
and tests Debug with ASan/UBSan, runs `clang-tidy`, builds Release, smoke-runs
the benchmark executable, and runs the Release tests.
