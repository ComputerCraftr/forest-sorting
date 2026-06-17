# forest-sorting

Header-only C++20 library for sorting a forest of nodes deterministically by
computed depth, then by node ID with adaptive radix sorting.

The production sorter is tuned for common depths `0-30`, handles sparse
outliers without dense depth-bucket allocations, and rejects depths that do not
fit the selected explicit depth prefix. IDs are exposed through fixed-width
most-significant-first bytes. The current production adaptive ID sorter walks
most-significant 64-bit chunks first, and sorts each chunk with stable LSD byte
passes; benchmark support also compares the same chunk engine with 1-byte and
4-byte chunks.
Duplicate full IDs are rejected.

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
permutations, verify the production adaptive chunk-radix sort against comparison,
LSD, byte-MSD, and chunk-MSD baselines, compare parent-index builders, compile
the portable algorithm header without UInt128 compatibility headers, and cover
duplicate full-ID rejection.

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
comparison plus fixed-prefix LSD, full-key byte-MSD, and adaptive chunk-MSD sort
variants with selectable datasets and output formats.

Parent builders are selected with `unordered`, `flat`, `control`, and `radix`.
By default, the benchmark runs every registered sort algorithm; `--sort all`
selects the same full set explicitly. Sort algorithms are selected with:
- `comparison`: `std::sort` over `depth || id`
- `depth-bucket-depth2-lsd`: dense vector-of-buckets by depth, then LSD per bucket
- `composite-depth2-lsd`: full composite key `depth[2] || id[16]`, sorted by LSD passes
- `depth-bucket-depth2-chunk-msd`: dense vector-of-buckets by depth, then ID chunk-MSD per bucket
- `composite-depth2-byte-msd-copyback`: full composite key `depth[2] || id[16]`, byte-MSD with copyback after each scatter
- `composite-depth2-byte-msd-lowcopy-branchy`: full composite key `depth[2] || id[16]`, byte-MSD using the previous branchy low-copy core
- `composite-depth2-byte-msd-lowcopy-flattened`: full composite key `depth[2] || id[16]`, byte-MSD using the flattened low-copy core
- `composite-depth2-byte-msd-lowcopy-batched`: full composite key `depth[2] || id[16]`, byte-MSD using depth-limited batched low-copy
- `adaptive-depth2-u32-chunk-msd-no-dense`: depth-MSD grouping + adaptive 4-byte ID chunk-MSD, dense shortcut disabled
- `adaptive-depth2-u32-chunk-msd`: production-style adaptive path, dense grouping if safe, locked to 2-byte depth
- `adaptive-depth2-u8-chunk-msd`: adaptive path using the unified chunk engine with 1-byte ID chunks
- `adaptive-depth2-u64-chunk-msd`: adaptive path using the unified chunk engine with 8-byte ID chunks
- `adaptive-depth2-u8-chunk-msd-touched-counts`: opt-in 1-byte chunk benchmark using thresholded touched/generation radix counters
- `adaptive-depth2-u32-chunk-msd-touched-counts`: opt-in 4-byte chunk benchmark using thresholded touched/generation radix counters
- `adaptive-depth2-u64-chunk-msd-touched-counts`: opt-in 8-byte chunk benchmark using thresholded touched/generation radix counters
- `adaptive-depth2-u64-chunk-msd-binary-small`: 8-byte chunk-MSD adaptive path with stable binary-insertion sort for small equal-depth ID ranges
- `adaptive-depth4-u32-chunk-msd`: production-style adaptive path, configured for a 4-byte depth prefix

The `adaptive-depth2-u32-chunk-msd` benchmark represents the current production-style adaptive
path: adaptive depth grouping followed by the unified chunk scheduler with
MSB-first 4-byte ID chunks and stable LSD byte passes inside each chunk. The
`adaptive-depth2-u8-chunk-msd` and `adaptive-depth2-u64-chunk-msd` rows use the same
scheduler with 1-byte and 8-byte chunks. The `adaptive-depth2-u32-chunk-msd-no-dense`
variant explicitly disables the dense grouping shortcut to measure its benefit.
The `*-touched-counts` rows are explicit opt-in A/B variants of the same chunk
scheduler that only change radix counter initialization policy for medium
ranges; large ranges still use full-clear counters. They are not included in
default or `--sort all` output.
The `composite-depth2-byte-msd-copyback`,
`composite-depth2-byte-msd-lowcopy-branchy`,
`composite-depth2-byte-msd-lowcopy-flattened`, and
`composite-depth2-byte-msd-lowcopy-batched` rows compare old copyback, previous
branchy low-copy, flattened low-copy, and depth-limited batched low-copy over
the same full 18-byte combined depth and ID key.

Benchmark rows use retained samples rather than one averaged run. The default
is `--iterations 7 --warmup 1`; table output shows median timings, CSV/TSV
include summary statistics and 95% bootstrap confidence intervals, and JSON
defaults to compact summary output without raw sample arrays. Use
`--sample-output raw` only when debugging individual samples, or
`--sample-output none` for compact status/delta-only JSON. Use
`--baseline-sort` or `--baseline-parent` for A/B deltas against a selected
algorithm or parent builder. Winner fields are CI-aware: a candidate only wins
when the paired percentage-delta interval is entirely below zero, a baseline
only wins when it is entirely above zero, and noisy overlaps report `tie`.
Repeat `--data-seed` to compare across multiple generated forests, and use
`--order-seed` for shuffled benchmark execution order.

```bash
cmake --build --preset release --target forest-sorting-bench
./out/build/release/benchmarks/forest-sorting-bench
./out/build/release/benchmarks/forest-sorting-bench --format csv --size 10000 --dataset random --parent all --sort adaptive-depth2-u32-chunk-msd
./out/build/release/benchmarks/forest-sorting-bench --size 10000 --dataset random --sort depth-bucket-depth2-lsd --sort depth-bucket-depth2-chunk-msd --sort adaptive-depth2-u8-chunk-msd --sort adaptive-depth2-u32-chunk-msd --sort adaptive-depth2-u64-chunk-msd --sort adaptive-depth2-u64-chunk-msd-binary-small
./out/build/release/benchmarks/forest-sorting-bench --size 100000 --dataset random --parent control --sort composite-depth2-byte-msd-copyback --sort composite-depth2-byte-msd-lowcopy-branchy --sort composite-depth2-byte-msd-lowcopy-flattened --sort composite-depth2-byte-msd-lowcopy-batched --baseline-sort composite-depth2-byte-msd-copyback --iterations 30 --warmup 3 --shuffle --order-seed 0x5eed --data-seed 1 --data-seed 2 --data-seed 3 --format json --sample-output summary
./out/build/release/benchmarks/forest-sorting-bench --size 100000 --dataset random --parent control --sort adaptive-depth2-u8-chunk-msd --sort adaptive-depth2-u32-chunk-msd --sort adaptive-depth2-u64-chunk-msd --baseline-sort adaptive-depth2-u32-chunk-msd --iterations 11 --warmup 2 --shuffle --order-seed 0x5eed --data-seed 0x5eed1234
./out/build/release/benchmarks/forest-sorting-bench --format json --sample-output summary --size 100000 --dataset random --parent control --sort adaptive-depth2-u8-chunk-msd --sort adaptive-depth2-u32-chunk-msd --sort adaptive-depth2-u64-chunk-msd --baseline-sort adaptive-depth2-u32-chunk-msd --iterations 30 --warmup 3 --shuffle --order-seed 0x5eed --data-seed 1 --data-seed 2 --data-seed 3
./out/build/release/benchmarks/forest-sorting-bench --format json --sample-output summary --size 10000 --size 100000 --size 1000000 --dataset random --dataset same-high64 --dataset outliers --parent control --sort adaptive-depth2-u8-chunk-msd --sort adaptive-depth2-u8-chunk-msd-touched-counts --sort adaptive-depth2-u32-chunk-msd --sort adaptive-depth2-u32-chunk-msd-touched-counts --sort adaptive-depth2-u64-chunk-msd --sort adaptive-depth2-u64-chunk-msd-touched-counts --baseline-sort adaptive-depth2-u32-chunk-msd --iterations 100 --warmup 5 --shuffle --order-seed 0x5eed --data-seed 1 --data-seed 2 --data-seed 3 --data-seed 4 --data-seed 5
./out/build/release/benchmarks/forest-sorting-bench --format json --sample-output raw --size 100000 --dataset random --parent flat --parent control --sort adaptive-depth2-u32-chunk-msd --baseline-parent control --iterations 11 --warmup 2 --data-seed 0x5eed1234
```

CI runs on GitHub Actions for macOS and Ubuntu using these presets. It builds
and tests Debug with ASan/UBSan, runs `clang-tidy`, builds Release, smoke-runs
the benchmark executable, and runs the Release tests.
