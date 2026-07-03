# forest-sorting

Header-only C++20 library for sorting a forest of nodes deterministically by
computed depth, then by node ID with adaptive radix sorting.

The production sorter is tuned for common depths `0-30`, handles sparse
outliers without dense depth-bucket allocations, and rejects depths that do not
fit the selected explicit depth prefix. IDs are exposed through fixed-width
most-significant-first bytes. Production sorting obtains one global ID order
from the radix parent join, then stably groups that permutation by depth. The
shared ID radix engine uses most-significant packed 32-bit radix partition
chunks with stable byte-LSD passes inside each chunk. This width is independent
of cached comparison chunk storage.
Duplicate full IDs are rejected.

Parent ID lookup defaults to a hash-free radix join. Its retained global ID
permutation is reused by computed-depth sorting, so duplicate detection, parent
joining, and final ordering share one ID radix sort. Bounded control-byte and
flat hash tables exist only in test/benchmark support. The rest of the sort and
verifier operate on integer-indexed vectors.

## API

The portable generic algorithm header does not depend on `unsigned __int128`:

```cpp
#include <forest_sorting/algorithms.hpp>
```

Portable users provide their own node and ID representation, plus a trait that
exposes fixed-width MSB-first ID bytes and parent lookup. Equality, ordering,
and chunk-extraction acceleration hooks are optional; the library falls back to
native operators or MSB-first byte/chunk comparison when those hooks are absent. Parent sentinel
handling is also optional: a missing parent ID is treated as `no_parent`, while
an optional `is_parent_sentinel` hook can mark a parent value as an explicit
root sentinel.

The shipped library contains no hash implementation or hash-table parent
builder. Benchmark support supplies deterministic FNV and finalizer traits for
the optional control and flat comparators; those hashes are not adversarially
hardened. The control comparator falls back to the single shipped radix join
when bounded probing exceeds its limit.

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

This repository uses CMake presets, the Ninja generator, and a C++20 `clang++`
available on `PATH`.

```bash
cmake --preset release            # generate Release Ninja files into out/build/release
cmake --build --preset release    # compile optimized binaries
./out/build/release/src/forest-sorting
```

On macOS with Homebrew LLVM, put LLVM first on `PATH` before configuring:

```bash
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
```

## Tests

```bash
cmake --preset debug              # generate Debug Ninja files with ASan/UBSan enabled
cmake --build --preset debug      # compile with sanitizers
ctest --preset debug              # run the test suite with sanitizers active
```

The regression tests check deterministic ordering across multiple input
permutations, verify the production global-ID-first sort against comparison,
LSD, byte-MSD, and chunk-MSD baselines, compare parent-index builders, compile
the portable algorithm header without UInt128 compatibility headers, and cover
duplicate full-ID rejection.

## Linting

CMake always defines a `tidy` target. It auto-detects `clang-tidy` from `PATH`
and common Homebrew LLVM locations; if none is found, the target fails with a
message explaining how to set `FOREST_CLANG_TIDY`.

```bash
cmake --preset debug
cmake --build --preset debug --target tidy
```

The tidy target runs against the shipped headers, test support headers, tests,
benchmarks, and executable sources using the generated `compile_commands.json`.
On macOS it also passes the active SDK sysroot to `clang-tidy`. To use a
specific binary, configure with:

```bash
cmake --preset debug -DFOREST_CLANG_TIDY=/path/to/clang-tidy
```

## Benchmarks

The release build includes a deterministic benchmark matrix comparing parent
index construction with `std::unordered_map`, the original flat hash, the
control-byte parent builder with bounded probing and radix fallback, and direct
radix join baselines. It can also time comparison plus fixed-prefix LSD,
full-key byte-MSD, and adaptive chunk-MSD sort variants with selectable datasets
and output formats.

Parent builders are selected with `unordered`, `flat`, `control`,
`control-finalizer-hash`, `radix-join-id-msd-chunk8`,
`radix-join-id-msd-chunk16`, `radix-join-id-msd-chunk32`,
`radix-join-id-msd-chunk64`,
`radix-join-id-msd-range-ladder-chunk8-le1024-chunk16-le16384-chunk32-otherwise`,
`radix-join-id-msd-range-ladder-chunk8-le2048-chunk16-le32768-chunk32-otherwise`,
`radix-join-id-msd-range-ladder-chunk8-le4096-chunk16-le65536-chunk32-otherwise`,
`radix-join-id-msd-byte-partition-core`,
`radix-directory-id-msd-chunk32-prefix8`, and
`radix-directory-id-msd-chunk32-prefix16`.
`radix-join-id-msd-chunk32` is the production default: it sorts stationary ID
and parent-query index permutations with the same packed 32-bit radix chunks
used by the sorter and retains the global ID permutation for final stable depth
grouping. `control` is the support-only bounded
control-byte hash-table comparator; it falls back to the shipped radix join if
probe chains become pathological. `control-finalizer-hash` is a
diagnostic control-table hash-policy comparator. The opt-in
`radix-join-id-msd-byte-partition-core` comparator sorts the same permutations
with byte-MSD and is excluded from `--parent default`.
`radix-join-id-msd-chunk8` and
`radix-join-id-msd-byte-partition-core` produce the same byte-MSD ID ordering.
The chunk8 row uses the unified MSD-chunk scheduler with
`RadixChunkBytes = 1`, the production counter policy, and the linear-small32
cutoff; the byte-partition-core row uses the generic byte-MSD partition
implementation without that scheduler or an equivalent small-range policy.
The range-ladder radix-join rows use the same parent join and retained
permutation contract as chunk32, but choose 1-byte, 2-byte, or 4-byte radix
chunks per pending ID-prefix range before the neutral
`global-id-permutation-then-depth-stable` sort consumes the permutation.
The radix-directory rows sort IDs once with the production chunk32 scheduler,
build a fixed high-prefix directory over the retained ID permutation, and
resolve parent IDs by per-parent lookup inside the matching prefix range. They
are a separate parent-lookup family from radix join: radix join sorts IDs plus
parent queries and merge-joins them; radix directory sorts IDs once and uses a
prefix directory plus in-range searches; `control` and `flat` are hash-table
lookup comparators.
Datasets include `random`, `outliers`, `same-high64`, `same-high32`,
`sequential`, `external-parents`, and `siblings`. By default, the benchmark
runs the curated default sort set: `global-id-permutation-then-depth-stable`,
`depth2-first-then-id-msd-chunk32-bitmask-le512`, and `comparison`. `--sort default`
selects this curated set explicitly; all other sort rows are opt-in and excluded
from the defaults. Sort algorithms are selected with:

- `comparison`: `std::sort` over `depth || id`
- `dense-depth2-buckets-then-id-lsd`: dense vector-of-buckets by depth, then LSD per bucket (previously `depth-bucket-depth2-lsd`)
- `composite-depth2-id-lsd`: full composite key `depth[2] || id[16]`, sorted by LSD passes (previously `composite-depth2-lsd`)
- `dense-depth2-buckets-then-id-msd-chunk64-full-clear`: dense vector-of-buckets by depth, then ID MSD per bucket (previously `depth-bucket-depth2-chunk-msd`)
- `composite-depth2-id-byte-msd-copyback`: full composite key `depth[2] || id[16]`, MSD with copyback after each scatter (previously `composite-depth2-byte-msd-copyback`)
- `composite-depth2-id-byte-msd-lowcopy-branchy`: full composite key `depth[2] || id[16]`, MSD using the previous branchy low-copy core (previously `composite-depth2-byte-msd-lowcopy-branchy`)
- `composite-depth2-id-byte-msd-lowcopy-flattened`: full composite key `depth[2] || id[16]`, MSD using the flattened low-copy core (previously `composite-depth2-byte-msd-lowcopy-flattened`)
- `composite-depth2-id-byte-msd-lowcopy-batched`: full composite key `depth[2] || id[16]`, MSD using depth-limited batched low-copy (previously `composite-depth2-byte-msd-lowcopy-batched`)
- `depth2-first-then-id-msd-chunk32-bitmask-le512-no-dense`: depth-MSD grouping + packed 32-bit ID radix chunks, dense shortcut disabled
- `depth2-first-then-id-msd-chunk32-bitmask-le512`: depth-first comparator that groups by depth and runs packed 32-bit ID radix chunks independently inside each depth range
- `global-id-permutation-then-depth-stable`: production row that consumes a retained global ID permutation when the parent builder provides one, otherwise computes one in the sort phase, then stably groups it by depth
- `depth2-first-then-id-msd-chunk32-full-clear`: old full-clear counter comparator for the production-style 4-byte path
- `depth2-first-then-id-msd-chunk8-full-clear`: depth-first path using packed 8-bit ID radix chunks and full-clear counters
- `depth2-first-then-id-msd-chunk8-bitmask-le512`: depth-first path using packed 8-bit ID radix chunks and bitmask-le512 counters
- `depth2-first-then-id-msd-chunk16-full-clear`: opt-in depth-first comparator using packed 16-bit ID radix chunks and full-clear counters
- `depth2-first-then-id-msd-chunk16-bitmask-le512`: opt-in depth-first comparator using packed 16-bit ID radix chunks and bitmask-le512 counters
- `depth2-first-then-id-msd-chunk64-full-clear`: depth-first path using packed 64-bit ID radix chunks and full-clear counters
- `depth4-first-then-id-msd-chunk32-full-clear`: production-style depth-first path, configured for a 4-byte depth prefix
- `depth2-first-then-id-msd-chunk32-bitmask-le128`, `depth2-first-then-id-msd-chunk32-bitmask-le256`, `depth2-first-then-id-msd-chunk32-bitmask-le1024`, `depth2-first-then-id-msd-chunk32-bitmask-le4096`: opt-in Track B2 variants using branch-free bitmask touched bucket counters for ranges up to the named threshold
- `depth2-first-then-id-range-ladder-chunk8-le1024-chunk16-le16384-chunk32-otherwise-*`, `depth2-first-then-id-range-ladder-chunk8-le2048-chunk16-le32768-chunk32-otherwise-*`, `depth2-first-then-id-range-ladder-chunk8-le4096-chunk16-le65536-chunk32-otherwise-*`: opt-in range-local chunk ladders; `full-clear` rows isolate chunk width and `bitmask-le512` rows use the production counter policy

Main forest benchmark labels describe full pipeline order. Tail-sort variants are measured by the tail microbench (`tail_microbench.cpp`), not registered as full forest pipeline rows, because they are local small-range policy experiments.

The `depth2-first-then-id-msd-chunk32-bitmask-le512` benchmark preserves the former depth-first adaptive path as a comparator: adaptive depth grouping followed by the unified chunk scheduler with MSB-first 4-byte ID chunks, stable byte-LSD passes inside each chunk, and one shared `bitmask-le512` radix count policy for depth grouping and ID chunk partitioning.
The production `global-id-permutation-then-depth-stable` row uses one global ID order, then stable dense/sparse depth grouping to preserve ID order within each depth. The parent benchmark row owns how that ID permutation is produced.
The `depth2-first-then-id-msd-chunk32-full-clear` row preserves the previous full-clear counter path as an explicit comparator. The `depth2-first-then-id-msd-chunk8-full-clear` and `depth2-first-then-id-msd-chunk64-full-clear` rows use the same scheduler with 1-byte and 8-byte chunks. The `depth2-first-then-id-msd-chunk32-bitmask-le512-no-dense` variant explicitly disables the dense grouping shortcut to measure its benefit.
The remaining `*-bitmask-le*` rows are explicit opt-in A/B variants of the same chunk scheduler. The bitmask-threshold rows replace the rejected generation-table touched-count loop with bitmask bucket tracking and only change radix counter initialization policy for ranges up to the named threshold; larger ranges still use full-clear counters. The 512-node cap is represented by the depth-first comparator label.
The `same-high32` dataset keeps the top 32 ID bits fixed while varying lower bits, which is useful for checking whether wider small-tail thresholds regress when many IDs share the first chunk32 radix partition.
The `composite-depth2-id-byte-msd-copyback`, `composite-depth2-id-byte-msd-lowcopy-branchy`, `composite-depth2-id-byte-msd-lowcopy-flattened`, and `composite-depth2-id-byte-msd-lowcopy-batched` rows compare old copyback, previous branchy low-copy, flattened low-copy, and depth-limited batched low-copy over the same full 18-byte combined depth and ID key.

Benchmark rows use retained samples rather than one averaged run. The default
is `--iterations 7 --warmup 1`; table output shows median timings, CSV/TSV
include summary statistics and 95% bootstrap confidence intervals, and JSON
defaults to compact summary output without raw sample arrays. Use
`--sample-output raw` only when debugging individual samples, or
`--sample-output none` for compact status/delta-only JSON. Use
`--baseline-sort` or `--baseline-parent` for A/B deltas against a selected
algorithm or parent builder. Supplying both also reports paired end-to-end
`pipeline_ms = parent_ms + sort_ms` deltas against the exact baseline pair.
Winner fields are CI-aware: a candidate only wins
when the paired percentage-delta interval is entirely below zero, a baseline
only wins when it is entirely above zero, and noisy overlaps report `tie`.
Repeat `--data-seed` to compare across multiple generated forests, and use
`--order-seed` for shuffled benchmark execution order.

```bash
cmake --build --preset release --target forest-sorting-bench

# Run default suite (uses default options)
./out/build/release/benchmarks/forest-sorting-bench

# Run one production parent/sort row
./out/build/release/benchmarks/forest-sorting-bench \
  --format csv \
  --size 10000 \
  --dataset random \
  --parent default \
  --sort global-id-permutation-then-depth-stable

# Run multiple sizes, datasets, and custom seeds with bootstrapped delta comparison
./out/build/release/benchmarks/forest-sorting-bench \
  --format json --sample-output summary \
  --size 10000 --size 100000 --size 1000000 \
  --dataset random --dataset same-high32 --dataset same-high64 --dataset outliers \
  --parent radix-join-id-msd-chunk8 \
  --parent radix-join-id-msd-chunk16 \
  --parent radix-join-id-msd-chunk32 \
  --parent radix-join-id-msd-chunk64 \
  --parent radix-join-id-msd-byte-partition-core \
  --baseline-parent radix-join-id-msd-chunk32 \
  --sort depth2-first-then-id-msd-chunk32-bitmask-le512 \
  --iterations 30 --warmup 3 --shuffle \
  --order-seed 0x5eed \
  --data-seed 1 --data-seed 2 --data-seed 3 --data-seed 4 --data-seed 5

# Compare the former depth-first and production global-ID-first pipelines
./out/build/release/benchmarks/forest-sorting-bench \
  --format json --sample-output summary \
  --size 10000 --size 100000 --size 1000000 \
  --dataset random --dataset same-high32 --dataset same-high64 --dataset outliers \
  --parent control --parent radix-join-id-msd-chunk32 \
  --sort depth2-first-then-id-msd-chunk32-bitmask-le512 \
  --sort global-id-permutation-then-depth-stable \
  --baseline-parent control \
  --baseline-sort depth2-first-then-id-msd-chunk32-bitmask-le512 \
  --iterations 50 --warmup 10 --shuffle \
  --order-seed 0x5eed \
  --data-seed 1 --data-seed 2 --data-seed 3 --data-seed 4 --data-seed 5

# Compare radix-join and radix-directory parent lookup families
./out/build/release/benchmarks/forest-sorting-bench \
  --format json --sample-output summary \
  --size 10000 --size 100000 --size 1000000 \
  --dataset random --dataset same-high32 --dataset same-high64 --dataset outliers \
  --parent radix-join-id-msd-chunk32 \
  --parent radix-directory-id-msd-chunk32-prefix8 \
  --parent radix-directory-id-msd-chunk32-prefix16 \
  --baseline-parent radix-join-id-msd-chunk32 \
  --sort global-id-permutation-then-depth-stable \
  --iterations 30 --warmup 3 --shuffle \
  --order-seed 0x5eed \
  --data-seed 1 --data-seed 2 --data-seed 3 --data-seed 4 --data-seed 5

# Compare parent radix range ladders while reusing the retained global ID permutation
./out/build/release/benchmarks/forest-sorting-bench \
  --format json --sample-output summary \
  --size 10000 --size 100000 --size 1000000 \
  --dataset random --dataset same-high32 --dataset same-high64 --dataset outliers --dataset siblings \
  --parent radix-join-id-msd-chunk32 \
  --parent radix-join-id-msd-range-ladder-chunk8-le1024-chunk16-le16384-chunk32-otherwise \
  --parent radix-join-id-msd-range-ladder-chunk8-le2048-chunk16-le32768-chunk32-otherwise \
  --parent radix-join-id-msd-range-ladder-chunk8-le4096-chunk16-le65536-chunk32-otherwise \
  --sort global-id-permutation-then-depth-stable \
  --baseline-parent radix-join-id-msd-chunk32 \
  --baseline-sort global-id-permutation-then-depth-stable \
  --iterations 30 --warmup 5 --shuffle \
  --order-seed 0x5eed \
  --data-seed 1 --data-seed 2 --data-seed 3 --data-seed 4 --data-seed 5

# Compare various chunk sizes and baseline sorting algorithms
./out/build/release/benchmarks/forest-sorting-bench \
  --size 10000 \
  --dataset random \
  --sort dense-depth2-buckets-then-id-lsd \
  --sort composite-depth2-id-lsd \
  --sort depth2-first-then-id-msd-chunk8-full-clear \
  --sort depth2-first-then-id-msd-chunk32-bitmask-le512 \
  --sort depth2-first-then-id-msd-chunk64-full-clear

# Compare composite depth-MSD partition and low-copy variants
./out/build/release/benchmarks/forest-sorting-bench \
  --size 100000 \
  --dataset random \
  --parent control \
  --sort composite-depth2-id-byte-msd-copyback \
  --sort composite-depth2-id-byte-msd-lowcopy-branchy \
  --sort composite-depth2-id-byte-msd-lowcopy-flattened \
  --sort composite-depth2-id-byte-msd-lowcopy-batched \
  --baseline-sort composite-depth2-id-byte-msd-copyback \
  --iterations 30 --warmup 3 --shuffle \
  --order-seed 0x5eed \
  --data-seed 1 --data-seed 2 --data-seed 3 \
  --format json --sample-output summary

# Compare different tail options (linear vs binary)
./out/build/release/benchmarks/forest-sorting-tail-bench \
  --format json --baseline-sort linear \
  --sort linear \
  --sort binary \
  --size 32 \
  --iterations 50 --warmup 10 \
  --ranges 1000 \
  --pattern random --pattern same-high32 --pattern same-high64 \
  | jq > test_tail_matrix.json
```

### Tail Microbenchmarks

To isolate and test the performance of the small-range tail sorting algorithms (avoiding parent setup, depth grouping, and radix partition noise), use `forest-sorting-tail-bench`. This benchmark runs arbitrary range sizes (defaulting to sizes `4`, `8`, `16`, `24`, `32`) and measures the sorting throughput (in nanoseconds per range) of:

- `linear`
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
and tests Debug with ASan/UBSan, runs `clang-tidy`, builds Release, and runs
the Release tests. CI smoke-runs the forest and tail benchmark CLIs with tiny
iteration counts to verify parsing, registry wiring, and output formats; it
does not run performance benchmarks.
