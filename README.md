# forest-sorting

Header-only C++20 library for sorting a forest of nodes deterministically by
computed depth, then by node ID with an adaptive byte-width radix sort.

The production sorter is tuned for common depths `0-30`, handles sparse
outliers without dense depth-bucket allocations, and rejects depths that do not
fit the selected explicit depth prefix. IDs are exposed through fixed-width
most-significant-first bytes; the implementation can chunk those bytes
internally for speed. Duplicate full IDs are rejected.

Parent ID lookup uses a control-byte open-addressed ID-to-index hash table,
then the rest of the sort and verifier operate on integer-indexed vectors.

## API

The portable generic algorithm header does not depend on `unsigned __int128`:

```cpp
#include <forest_sorting/algorithms.hpp>
```

Portable users provide their own node and ID representation, plus a trait that
exposes ID bytes, hashing, equality, parent lookup, and root-parent semantics.
The hashing policy is caller-controlled; for high-risk applications receiving
adversarial IDs, it is recommended to provide a keyed or otherwise hardened
hash in the traits.

The primary primitive returns a non-owning sorted index order:

```cpp
auto order = forest_sorting::sortedOrderByDepthAndId<2>(
    nodes, MyCombinedTraits{});
```

The template argument is the explicit depth-prefix byte count. For example,
`<2>` supports depths up to `65535` and rejects deeper forests. The untemplated
overload computes depths and dispatches to the smallest sufficient prefix from
`<1>` through `<4>`.

For callers that already have depths, the advanced API accepts precomputed
depths and derives the observed maximum internally:

```cpp
auto order = forest_sorting::sortedOrderByDepthAndIdWithDepths<2>(
    nodes, MyCombinedTraits{}, depths);
```

Convenience wrappers for sorted copy, in-place sorting, and verification are
thin layers over the same order primitive. The untemplated verifier uses full
`uint32_t` depth capacity; explicit verifier overloads enforce the selected
prefix width.

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
./out/build/release/src/forest-sorting
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
permutations, verify the production adaptive radix sort against comparison,
LSD, and MSD baselines, compare parent-index builders, compile the portable
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
comparison plus fixed-prefix LSD/MSD/adaptive sort variants with selectable
datasets and output formats.

Parent builders are selected with `unordered`, `flat`, `control`, and `radix`.
Sort algorithms are selected with `comparison`, `depth-bucket-depth2-lsd`,
`composite-depth2-lsd`, `depth-bucket-depth2-msd`, `composite-depth2-msd`,
`adaptive-depth2-msd`, `adaptive-depth2-msd-binary-small`, and
`adaptive-depth4-msd`.

```bash
cmake --build --preset release --target forest-sorting-bench
./out/build/release/benchmarks/forest-sorting-bench
./out/build/release/benchmarks/forest-sorting-bench --format csv --size 10000 --dataset random --parent all --sort adaptive-depth2-msd
./out/build/release/benchmarks/forest-sorting-bench --size 10000 --dataset random --sort depth-bucket-depth2-lsd --sort depth-bucket-depth2-msd --sort adaptive-depth2-msd --sort adaptive-depth2-msd-binary-small
```

CI runs on GitHub Actions for macOS and Ubuntu using these presets. It builds
and tests Debug with ASan/UBSan, runs `clang-tidy`, builds Release, smoke-runs
the benchmark executable, and runs the Release tests.
