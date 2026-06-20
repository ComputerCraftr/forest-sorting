# forest-sorting

Header-only C++20 library for sorting a forest of nodes deterministically by
computed depth, then by node ID with adaptive radix sorting.

The production sorter is tuned for common depths `0-30`, handles sparse
outliers without dense depth-bucket allocations, and rejects depths that do not
fit the selected explicit depth prefix. IDs are exposed through fixed-width
most-significant-first bytes. The current production adaptive ID sorter walks
most-significant 32-bit chunks first and sorts each chunk with stable LSD byte
passes; benchmark support also compares the same chunk engine with 1-byte and
8-byte chunks.
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
`<2>` computes depths into a `uint16_t` payload, supports depths up to `65535`,
and rejects deeper forests during depth generation. `<1>` uses `uint8_t`, while
`<3>` and `<4>` use `uint32_t`. The untemplated overload computes full-width
depths and dispatches to the smallest sufficient prefix from `<1>` through
`<4>`.

For callers that already have depths, the advanced API accepts precomputed
unsigned integral depths and derives the observed maximum internally. The
storage type must be at least as wide as the selected prefix, so `<2>` accepts
both `std::vector<uint16_t>` and the existing `std::vector<uint32_t>` form:

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
The production `radix` fallback sorts stationary ID/query records through
u32-chunk index permutations. The opt-in `radix-byte-msd` comparator sorts the
same permutations with byte-MSD and is excluded from `--parent default`.
Datasets include `random`, `outliers`, `same-high64`, `same-high32`,
`sequential`, `external-parents`, and `siblings`. By default, the benchmark
runs the standard sort set; `--sort all` selects that same set explicitly and
excludes opt-in tuning experiments. Sort algorithms are selected with:

- `comparison`: `std::sort` over `depth || id`
- `depth-bucket-depth2-lsd`: dense vector-of-buckets by depth, then LSD per bucket
- `composite-depth2-lsd`: full composite key `depth[2] || id[16]`, sorted by LSD passes
- `depth-bucket-depth2-chunk-msd`: dense vector-of-buckets by depth, then ID chunk-MSD per bucket
- `composite-depth2-byte-msd-copyback`: full composite key `depth[2] || id[16]`, byte-MSD with copyback after each scatter
- `composite-depth2-byte-msd-lowcopy-branchy`: full composite key `depth[2] || id[16]`, byte-MSD using the previous branchy low-copy core
- `composite-depth2-byte-msd-lowcopy-flattened`: full composite key `depth[2] || id[16]`, byte-MSD using the flattened low-copy core
- `composite-depth2-byte-msd-lowcopy-batched`: full composite key `depth[2] || id[16]`, byte-MSD using depth-limited batched low-copy
- `adaptive-depth2-u32-chunk-msd-no-dense`: depth-MSD grouping + adaptive 4-byte ID chunk-MSD, dense shortcut disabled
- `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32`: default production label, maps to u32 chunk-MSD + bitmask-le512 + linear-small32 (also accepts compatibility alias `adaptive-depth2-u32-chunk-msd`)
- `adaptive-depth2-u32-chunk-msd-full-clear-tail-linear32`: old full-clear counter comparator for the production-style 4-byte chunk path
- `adaptive-depth2-u8-chunk-msd-full-clear-tail-linear32`: adaptive path using the unified chunk engine with 1-byte ID chunks and full-clear counters
- `adaptive-depth2-u8-chunk-msd-bitmask-le512-tail-linear32`: adaptive path using 1-byte ID chunks and bitmask-le512 counters
- `adaptive-depth2-u16-chunk-msd-full-clear-tail-linear32`: opt-in adaptive comparator using 2-byte ID chunks and full-clear counters
- `adaptive-depth2-u16-chunk-msd-bitmask-le512-tail-linear32`: opt-in adaptive comparator using 2-byte ID chunks and bitmask-le512 counters
- `adaptive-depth2-u64-chunk-msd-full-clear-tail-linear32`: adaptive path using the unified chunk engine with 8-byte ID chunks and full-clear counters
- `adaptive-depth2-u64-chunk-msd-full-clear-tail-binary32`: 8-byte chunk-MSD adaptive path with stable binary-insertion sort for small equal-depth ID ranges
- `adaptive-depth4-u32-chunk-msd-full-clear-tail-linear32`: production-style adaptive path, configured for a 4-byte depth prefix
- `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear16`, `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear48`: opt-in tail-threshold variants that keep the production radix count policy and only change the linear small-range cutoff; `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32` is the production row
- `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32-chunk-cache`: opt-in linear-small32 contender that caches MSB-first 64-bit ID chunks before insertion sorting
- `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-binary32`: opt-in binary-insertion tail using cached MSB-first 64-bit ID chunks
- `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-exponential16`, `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-exponential32`, `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-exponential48`: opt-in exponential insertion-search variants using threshold-sized cached MSB-first 64-bit ID chunks under the production `bitmask-le512` radix count policy
- `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-branchless-bitwise16`, `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-branchless-bitwise32`, `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-branchless-bitwise48`: opt-in fixed-pass base-2 stable-radix small-tail variants under the production `bitmask-le512` radix count policy
- `adaptive-depth2-u32-chunk-msd-bitmask-le128-tail-linear32`, `adaptive-depth2-u32-chunk-msd-bitmask-le256-tail-linear32`, `adaptive-depth2-u32-chunk-msd-bitmask-le1024-tail-linear32`, `adaptive-depth2-u32-chunk-msd-bitmask-le4096-tail-linear32`: opt-in Track B2 variants using branch-free bitmask touched bucket counters for ranges up to the named threshold
- `adaptive-depth2-range-ladder-u8-le1024-u16-le16384-*-tail-linear32`, `adaptive-depth2-range-ladder-u8-le2048-u16-le32768-*-tail-linear32`, `adaptive-depth2-range-ladder-u8-le4096-u16-le65536-*-tail-linear32`: opt-in range-local chunk ladders; `full-clear` rows isolate chunk width and `bitmask-le512` rows use the production counter policy

The `adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32` benchmark represents the current production-style adaptive
path: adaptive depth grouping followed by the unified chunk scheduler with
MSB-first 4-byte ID chunks, stable LSD byte passes inside each chunk, and
one shared `bitmask-le512` radix count policy for depth grouping and ID chunk partitioning: touched-bucket counters are used for radix ranges up to 512 nodes, and larger ranges fall back to full-clear counters.
The `adaptive-depth2-u32-chunk-msd-full-clear-tail-linear32` row preserves the previous full-clear
counter path as an explicit comparator. The `adaptive-depth2-u8-chunk-msd-full-clear-tail-linear32` and
`adaptive-depth2-u64-chunk-msd-full-clear-tail-linear32` rows use the same scheduler with 1-byte and
8-byte chunks. The `adaptive-depth2-u32-chunk-msd-no-dense` variant explicitly
disables the dense grouping shortcut to measure its benefit.
The remaining `*-bitmask-le*` rows are explicit opt-in A/B variants of the
same chunk scheduler. Tail experiment rows keep the same production
`bitmask-le512` radix count policy and vary only the small-tail algorithm or
threshold named after `tail-`. The bitmask-threshold rows replace the rejected
generation-table touched-count
loop with bitmask bucket tracking and only change radix counter initialization
policy for ranges up to the named threshold; larger ranges still use full-clear
counters. The 512-node cap is represented by the production label. These variants are not included in default or
`--sort all` output.
The `same-high32` dataset keeps the top 32 ID bits fixed while varying lower
bits, which is useful for checking whether wider small-tail thresholds regress
when many IDs share the first u32 chunk.
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

# Run default suite (uses default options)
./out/build/release/benchmarks/forest-sorting-bench

# Run single dataset and specific algorithm
./out/build/release/benchmarks/forest-sorting-bench \
  --format csv \
  --size 10000 \
  --dataset random \
  --parent default \
  --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32

# Run multiple sizes, datasets, and custom seeds with bootstrapped delta comparison
./out/build/release/benchmarks/forest-sorting-bench \
  --format json --sample-output summary \
  --size 10000 --size 100000 --size 1000000 \
  --dataset random --dataset same-high32 --dataset same-high64 \
  --parent radix --parent radix-byte-msd \
  --baseline-parent radix-byte-msd \
  --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32 \
  --iterations 30 --warmup 3 --shuffle \
  --order-seed 0x5eed \
  --data-seed 1 --data-seed 2 --data-seed 3 --data-seed 4 --data-seed 5

# Compare various chunk sizes and baseline sorting algorithms
./out/build/release/benchmarks/forest-sorting-bench \
  --size 10000 \
  --dataset random \
  --sort depth-bucket-depth2-lsd \
  --sort depth-bucket-depth2-chunk-msd \
  --sort adaptive-depth2-u8-chunk-msd-full-clear-tail-linear32 \
  --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32 \
  --sort adaptive-depth2-u64-chunk-msd-full-clear-tail-linear32 \
  --sort adaptive-depth2-u64-chunk-msd-full-clear-tail-binary32

# Compare composite depth-MSD partition and low-copy variants
./out/build/release/benchmarks/forest-sorting-bench \
  --size 100000 \
  --dataset random \
  --parent control \
  --sort composite-depth2-byte-msd-copyback \
  --sort composite-depth2-byte-msd-lowcopy-branchy \
  --sort composite-depth2-byte-msd-lowcopy-flattened \
  --sort composite-depth2-byte-msd-lowcopy-batched \
  --baseline-sort composite-depth2-byte-msd-copyback \
  --iterations 30 --warmup 3 --shuffle \
  --order-seed 0x5eed \
  --data-seed 1 --data-seed 2 --data-seed 3 \
  --format json --sample-output summary

# Compare different tail options (linear, binary, exponential, branchless-bitwise)
./out/build/release/benchmarks/forest-sorting-bench \
  --format json --sample-output summary --parent control \
  --size 10000 --size 100000 --size 1000000 \
  --dataset random --dataset same-high32 --dataset same-high64 --dataset outliers \
  --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32 \
  --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32-chunk-cache \
  --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-binary32 \
  --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-exponential32 \
  --sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-branchless-bitwise32 \
  --baseline-sort adaptive-depth2-u32-chunk-msd-bitmask-le512-tail-linear32 \
  --iterations 50 --warmup 10 --shuffle \
  --order-seed 0x5eed \
  --data-seed 1 --data-seed 2 --data-seed 3 --data-seed 4 --data-seed 5 \
  | jq > test_tail_matrix.json
```

### Tail Microbenchmarks

To isolate and test the performance of the small-range tail sorting algorithms (avoiding parent setup, depth grouping, and radix partition noise), use `forest-sorting-tail-bench`. This benchmark runs arbitrary range sizes (defaulting to sizes `4`, `8`, `16`, `24`, `32`) and measures the sorting throughput (in nanoseconds per range) of:

- `linear`
- `linear-chunk-cache`
- `binary`
- `exponential`
- `branchless-bitwise`

Example:

```bash
cmake --build --preset release --target forest-sorting-tail-bench
./out/build/release/benchmarks/forest-sorting-tail-bench \
  --iterations 100 \
  --warmup 10 \
  --ranges 1000 \
  --size 16 \
  --pattern random \
  --format table
```

CI runs on GitHub Actions for macOS and Ubuntu using these presets. It builds
and tests Debug with ASan/UBSan, runs `clang-tidy`, builds Release, smoke-runs
the benchmark executable, and runs the Release tests.
