# forest-sorting

Header-only C++20 library for deterministic forest ordering by `depth || id`.

Production behavior:

- Builds parent indexes with a hash-free radix join.
- Rejects duplicate full IDs.
- Computes parent-derived depth and rejects cycles.
- Retains the global ID permutation from the parent phase.
- Stably groups that permutation by depth.
- Uses packed 32-bit ID radix chunks by default.

Benchmark-only parent builders, sort policies, and hash-table comparisons live
under `benchmarks/support`. Independent correctness oracles and instrumented
traits live under `tests/support`.

## API

The portable API does not require `unsigned __int128`:

```cpp
#include <forest_sorting/algorithms.hpp>
```

Public operations:

```cpp
auto order = forest_sorting::sortedOrderByDepthAndId(nodes, traits);
auto orderWithDepths =
    forest_sorting::sortedOrderByDepthAndIdWithDepths(nodes, traits, depths);
auto copy = forest_sorting::sortedCopyByDepthAndId(nodes, traits);
forest_sorting::sortInPlaceByDepthAndId(nodes, traits);
bool valid = forest_sorting::verifySortedByDepthAndId(nodes, traits);
```

Container requirements are deliberately narrow:

- Indexed input: const `size()` and `operator[]`.
- Copy-returning API: copy-constructible node values.
- In-place API: mutable indexed access and movable, assignable, swappable
  values.
- Explicit depths: indexed unsigned-integral values other than `bool`.
- Iterators, range conformance, and contiguous storage are not required.

Supplied depths:

- Are validated before any ID or parent access.
- Must have exactly one value per node.
- Must not exceed `UINT32_MAX`.
- Are never recalculated from parent links.
- Dispatch to 1-, 2-, 3-, or 4-byte depth kernels from their observed maximum.

### Traits

Every traits type provides:

- `using Id`
- constant `id_byte_count`
- `id(node)`
- `parent_id(node)`
- `byte_msb_first(id, byteIndex)`

The canonical ID order is the lexicographic MSB-first byte order. Optional
hooks are acceleration hooks and must preserve those exact semantics:

- Ordering: `traits.less`, otherwise MSB-first bytes.
- Equality: `traits.equal`, otherwise MSB-first bytes.
- Root sentinel: `traits.is_parent_sentinel`, otherwise no ID is
  preclassified as a sentinel.
- Chunk access: `chunk_msb_first<1|2|4|8>`, independently detected per width,
  otherwise assembled from `byte_msb_first`.
- Native `operator<` and `operator==` are ignored.
- An unmatched parent ID resolves to `no_parent`.

The guarded UInt128 compatibility API is available when the compiler defines
`__SIZEOF_INT128__`:

```cpp
#include <forest_sorting/uint128_forest.hpp>
```

## Build

Repository policy:

- Ninja is required for top-level builds.
- Clang is the default compiler.
- Clang and GCC warnings are fatal.
- Debug enables ASan and UBSan.
- GCC is additional warning coverage, not the preferred toolchain.

```bash
cmake --fresh --preset debug
cmake --build --preset build-debug
ctest --preset test-debug --output-on-failure

cmake --fresh --preset release
cmake --build --preset build-release
ctest --preset test-release --output-on-failure

cmake --fresh --preset gcc-warnings
cmake --build --preset build-gcc-warnings
ctest --preset test-gcc-warnings --output-on-failure
```

Use `cmake --fresh --preset <name>` when a build tree was configured with a
different generator or compiler.

## Tooling

Every configure and build updates the ignored root `compile_commands.json`
symlink to the active build tree.

Clang-tidy runs in parallel and atomically publishes its complete log:

```bash
cmake --build --preset build-debug --target tidy
```

Formatting:

```bash
git ls-files -z '*.cpp' '*.cc' '*.cxx' '*.hpp' '*.hh' '*.h' |
  xargs -0 clang-format-22 --dry-run --Werror

git ls-files -z ':(glob)**/CMakeLists.txt' '*.cmake' |
  xargs -0 pipx run --spec cmakelang==0.6.13 cmake-format --check
```

## Full Benchmark

`forest-sorting-bench` measures parent construction, sorting, and total
pipeline time.

Representative parent families:

- Production radix join: `radix-join-id-msd-chunk32`
- Fixed-width comparisons: `radix-join-id-msd-chunk16`
- Radix directory: `radix-directory-id-msd-chunk32-prefix16`
- Opt-in whole-build size ladders:
  `radix-join-id-msd-size-ladder-chunk16-le16384-chunk32-otherwise`
- Hash comparisons: `control`, `flat`, and `unordered`

Representative sort families:

- Production: `global-id-permutation-then-depth-stable`
- Depth-first ID radix:
  `depth2-first-then-id-msd-chunk32-bitmask-le512`
- Direct comparison: `comparison`
- Opt-in per-depth-range ID ladders: sort labels containing `range-ladder`
- Diagnostic families: `dense-*`, `composite-*`, `*-full-clear`, and
  `*-bitmask-le*`

Datasets include `random`, `same-high32`, `same-high64`, `outliers`,
`siblings`, `sequential`, and `external-parents`.

Selector behavior is shared across registries:

- The first selector clears curated defaults.
- Concrete names append uniquely.
- `default` restores curated defaults.
- `all` selects the complete registry.
- A requested baseline is appended once if missing.
- Baseline-only runs are valid.
- Either parent or sort may stay fixed at its baseline while the other varies.

Parent-only A/B example:

```bash
./out/build/release/benchmarks/forest-sorting-bench \
  --format json --sample-output summary \
  --size 10000 --size 100000 \
  --dataset random --dataset same-high64 \
  --parent radix-join-id-msd-chunk16 \
  --parent radix-join-id-msd-chunk32 \
  --sort global-id-permutation-then-depth-stable \
  --baseline-parent radix-join-id-msd-chunk32 \
  --baseline-sort global-id-permutation-then-depth-stable \
  --iterations 30 --warmup 5 --shuffle \
  --order-seed 0x5eed --data-seed 1 --data-seed 2
```

Sort-only A/B example:

```bash
./out/build/release/benchmarks/forest-sorting-bench \
  --size 100000 --dataset random \
  --parent radix-join-id-msd-chunk32 \
  --sort comparison \
  --sort global-id-permutation-then-depth-stable \
  --baseline-parent radix-join-id-msd-chunk32 \
  --baseline-sort global-id-permutation-then-depth-stable
```

Use `--help` for the complete registry. Full output labels and schemas are
stable across this refactor.

## Tail Benchmark

`forest-sorting-tail-bench` isolates terminal ID ranges from the production MSD
sorter.

Workloads:

- `synthetic`: explicit adversarial patterns.
- `captured-node-ids`: production node-ID terminal ranges.
- `captured-parent-queries`: production parent-query terminal ranges.

Algorithms:

- `linear`: exact production `stableSortIndexRangeSmallLinear<32>` kernel.
- `binary`
- `exponential`
- `branchless-bitwise`
- `shell-gap-10-4-1`
- `shell-gap-3-2-1`
- `shell-gap-16-7-3-1`

Capture behavior:

- Uses the production MSD scheduler and a compile-time observer.
- Records ranges of size 2 through 32 before the production linear sort.
- Keeps node-ID and parent-query reservoirs independent and deterministic.
- Excludes corpus generation and verification from measured time.
- Reports an empty capture as a successful `empty` result.

Example:

```bash
./out/build/release/benchmarks/forest-sorting-tail-bench \
  --format json \
  --workload synthetic --workload captured-node-ids \
  --dataset random --dataset same-high64 \
  --source-size 100000 \
  --tail-size 8 --tail-size 16 --tail-size 32 \
  --tail-count 1000 \
  --algorithm linear --algorithm binary --algorithm shell-gap-10-4-1 \
  --baseline-algorithm linear \
  --iterations 100 --warmup 10 --shuffle \
  --order-seed 0x5eed --data-seed 1
```

Tail output uses:

- Configuration: `tail_count_limit`, `baseline_algorithm`
- Results: `source_size`, `tail_size`, `min_tail_size`, `max_tail_size`,
  `tail_count`, `algorithm`

The removed aliases `--size`, `--ranges`, `--sort`, `--baseline-sort`, and
`--seed` are rejected.

## CI

GitHub Actions covers:

- Ubuntu 26.04 with LLVM 22
- macOS Clang
- Debug ASan/UBSan tests
- Release tests
- parallel clang-tidy
- LLVM clang-format and pipx cmake-format
- Ubuntu GCC fatal-warning build and tests
- full and tail CLI smoke tests
