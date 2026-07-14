# forest-sorting

Header-only C++20 library for deterministic forest ordering:

- computes parent-derived depth
- sorts by `depth || id`
- exposes portable fixed-width MSB-first ID traits
- rejects duplicate full IDs
- reuses the parent phase's retained global ID permutation in the production
  sort path

Production defaults:

- parent lookup: hash-free radix join
- final order: global ID permutation, then stable depth grouping
- ID radix partitioning: packed 32-bit chunks with stable byte-LSD passes
- common depth shape: optimized for typical depths `0-30` while still handling
  sparse outliers

Hash-table parent builders, alternate radix rows, and diagnostic comparators
live in test/benchmark support. The shipped production path operates on
integer-indexed vectors and does not require a hash implementation.

## API

The portable generic algorithm header does not depend on `unsigned __int128`:

```cpp
#include <forest_sorting/algorithms.hpp>
```

Portable users provide:

- node and ID representation
- traits for node ID, parent ID, and fixed-width MSB-first ID bytes
- optional equality, ordering, and chunk-extraction acceleration hooks
- optional `is_parent_sentinel` for explicit root sentinels

Fallbacks:

- native `<` / `==` when available
- MSB-first byte/chunk comparison when trait/native comparison hooks are absent
- missing parent ID treated as `no_parent` when no explicit sentinel hook is
  provided

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

The release build provides two benchmark executables:

- `forest-sorting-bench`: full parent-build plus sort pipeline benchmarks
- `forest-sorting-tail-bench`: isolated small-range tail-sort microbenchmarks

Use `--help` on either executable for the complete, current label list. The
main registry is intentionally larger than the examples below because many rows
are opt-in A/B experiments.

Benchmark defaults:

- parent: `radix-join-id-msd-chunk32`
- sorts: `global-id-permutation-then-depth-stable`,
  `depth2-first-then-id-msd-chunk32-bitmask-le512`, and `comparison`
- samples: `--iterations 7 --warmup 1`
- JSON: compact summary output unless `--sample-output raw` is requested

Parent-builder families:

- Production radix join: fixed chunk rows such as
  `radix-join-id-msd-chunk16` and `radix-join-id-msd-chunk32`.
- Size-ladder radix join: opt-in rows choose chunk8/chunk16/chunk32 or
  chunk16/chunk32 once per ID permutation, then run the corresponding
  fixed-width kernel unchanged. For example,
  `radix-join-id-msd-size-ladder-chunk8-le1024-chunk16-le16384-chunk32-otherwise`
  and `radix-join-id-msd-size-ladder-chunk16-le10000-chunk32-otherwise`.
- Radix directory: `radix-directory-id-msd-chunk32-prefix8` and
  `radix-directory-id-msd-chunk32-prefix16`.
- Hash-table support comparators: `control`, `control-finalizer-hash`,
  `flat`, and `unordered`.

Sort-family examples:

- `global-id-permutation-then-depth-stable`: production row; consumes the
  retained parent ID permutation when available.
- `depth2-first-then-id-msd-chunk32-bitmask-le512`: depth-first comparator with
  packed 32-bit ID radix chunks inside each depth range.
- `comparison`: direct `std::sort` over `depth || id`.
- `dense-*`, `composite-*`, `*-full-clear`, `*-bitmask-le*`, and
  `*-range-ladder-*` rows are opt-in algorithm experiments.
  Sort range ladders select one width per equal-depth ID range and then run the
  corresponding fixed-width kernel unchanged.

Datasets:

- `random`
- `same-high32`
- `same-high64`
- `outliers`
- `siblings`
- `sequential`
- `external-parents`

Result interpretation:

- Table output shows medians.
- CSV/TSV include summary statistics and bootstrap confidence intervals.
- JSON supports `--sample-output summary`, `raw`, or `none`.
- `--baseline-parent` and `--baseline-sort` produce paired deltas.
- Supplying both baselines also reports paired pipeline deltas:
  `pipeline_ms = parent_ms + sort_ms`.
- Winner fields are confidence-interval aware; overlapping intervals report
  `tie`.

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

# Compare fixed parent chunk widths
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

# Compare parent size-ladder rows while reusing the retained global ID permutation
./out/build/release/benchmarks/forest-sorting-bench \
  --format json --sample-output summary \
  --size 10000 --size 100000 --size 1000000 \
  --dataset random --dataset same-high32 --dataset same-high64 --dataset outliers --dataset siblings \
  --parent radix-join-id-msd-chunk16 \
  --parent radix-join-id-msd-chunk32 \
  --parent radix-join-id-msd-size-ladder-chunk8-le1024-chunk16-le16384-chunk32-otherwise \
  --parent radix-join-id-msd-size-ladder-chunk16-le10000-chunk32-otherwise \
  --parent radix-directory-id-msd-chunk32-prefix16 \
  --sort global-id-permutation-then-depth-stable \
  --baseline-parent radix-join-id-msd-chunk32 \
  --baseline-sort global-id-permutation-then-depth-stable \
  --iterations 30 --warmup 5 --shuffle \
  --order-seed 0x5eed \
  --data-seed 1 --data-seed 2 --data-seed 3 --data-seed 4 --data-seed 5

# Compare representative sort families
./out/build/release/benchmarks/forest-sorting-bench \
  --size 10000 \
  --dataset random \
  --sort dense-depth2-buckets-then-id-lsd \
  --sort composite-depth2-id-lsd \
  --sort depth2-first-then-id-msd-chunk8-full-clear \
  --sort depth2-first-then-id-msd-chunk32-bitmask-le512 \
  --sort depth2-first-then-id-msd-chunk64-full-clear

# Compare composite byte-MSD with the production and depth-first families
./out/build/release/benchmarks/forest-sorting-bench \
  --size 100000 \
  --dataset random \
  --parent control \
  --sort composite-depth2-id-byte-msd-copyback \
  --sort global-id-permutation-then-depth-stable \
  --sort depth2-first-then-id-msd-chunk32-bitmask-le512 \
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

Use `forest-sorting-tail-bench` to isolate small-range sorting costs without
parent setup, depth grouping, or radix partition noise.

Defaults and options:

- default sizes: `4`, `8`, `16`, `24`, `32`
- unit: nanoseconds per range
- patterns include `random`, `same-high32`, and `same-high64`
- algorithms: `linear`, `binary`, `exponential`, `branchless-bitwise`

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

## CI

GitHub Actions runs on Ubuntu and macOS with the same presets:

- Debug configure/build with ASan/UBSan
- Debug tests
- `clang-tidy`
- Release configure/build
- Release tests
- benchmark CLI smoke tests with tiny iteration counts

CI smoke tests verify parsing, registry wiring, and output formats. They do not
run performance benchmark matrices.
