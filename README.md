# Dictionaries: Deterministic vs Randomized

Advanced Algorithms, Programming Assignment 2 (Theme 2: Deterministic vs Randomized).

We compare two ordered dictionaries (insert, lookup, erase, in-order and range iteration):

| Module | Algorithm | Kind | Primary author |
|---|---|---|---|
| [`red_black_tree/`](red_black_tree/) | Red-black tree | Deterministic | Aaron Falco |
| [`randomized/`](randomized/) | To be decided (e.g. skip list or treap) | Randomized | Laith |
| [`core/`](core/) | `std::map` adapter | Reference only (oracle + baseline, not one of the compared algorithms) | shared |

Beyond mean throughput, we compare the **distributions** of cost: per-operation structural cost (comparisons, nodes visited, restructuring steps) and run-to-run variation across random seeds on identical inputs. We also compare **code complexity**.

## Repository layout

```
core/include/dict/
  ordered_map.hpp       OrderedMap concept: the interface both implementations satisfy
  counters.hpp          OpCounters + DICT_COUNT macro (compiled out of timing builds)
  rng.hpp               xoshiro256** PRNG (identical output on every compiler)
  workloads.hpp         input families, generated up front from a seed
  std_map_adapter.hpp   std::map behind the interface (test oracle, benchmark baseline)
red_black_tree/         deterministic implementation
randomized/             randomized implementation (structure to be decided)
tests/                  correctness tests, run against every implementation
bench/                  benchmark harness (built as two binaries, see below)
scripts/                experiment runner and plotting
docs/                   predictions, report, reflections
```

## Building

Requires Visual Studio 2022 or later with the **Desktop development with C++** workload (it ships MSVC, CMake and Ninja).

**In Visual Studio:** File → Open → Folder… and pick the repo root. Choose the `x64 Debug` or `x64 Release` configuration from the toolbar, then Build → Build All. Tests appear in Test Explorer.

**From the command line:** open a *Developer PowerShell for VS* in the repo root:

```powershell
cmake --preset x64-release
cmake --build --preset x64-release
ctest --preset x64-release
```

Use `x64-debug` instead while developing. **Always benchmark the Release build.**

## Tests

`ctest` runs four suites: `infrastructure` (RNG and workload generator), `std_map`, `red_black_tree` and `randomized`. Each implementation is checked against `std::map` with:

- unit tests (empty map, insert/update, erase)
- 50,000 random operations diffed step by step against the oracle
- range queries
- a replay of every benchmark workload

Each implementation's `check_invariants()` is called throughout.

An implementation whose header has `implemented = false` shows as **Skipped**. Flip it to `true` once your implementation is ready.

To run one suite directly with full output:

```powershell
.\out\build\x64-debug\tests\dict_tests.exe red_black_tree
```

## Benchmarks

`bench/bench_main.cpp` is built twice:

| Binary | `DICT_INSTRUMENT` | Measures |
|---|---|---|
| `dict_bench` | off | wall-clock time per phase (counters compiled out) |
| `dict_bench_instrumented` | on | structural counters per phase, plus per-operation cost histograms |

Keeping the two builds separate means the instrumentation can never distort the timings. Timings from the instrumented binary are not meaningful.

Every implementation replays exactly the same operations. Each workload is generated from its input seed before timing starts.

- `--input-seeds K` varies the key sequence.
- `--algo-seeds S` varies the randomized structure's internal RNG while the input stays fixed.

The red-black tree ignores the algorithm seed, so for it this is just extra repetitions. Comparing the spread across algorithm seeds to the spread across input seeds separates randomness *in the algorithm* from randomness *in the input*.

Workload families (`core/include/dict/workloads.hpp`):

| Family | Phases |
|---|---|
| `uniform` | insert n random keys → n lookups (≈50% hits) → erase all in random order |
| `sorted` | insert 0..n-1 ascending → n lookups → erase ascending |
| `reverse` | insert descending → n lookups → erase descending |
| `sliding_window` | insert n random keys → n rounds of (insert new, erase oldest) |
| `zipf` | insert n random keys → n Zipf(s≈1)-skewed lookups |

Run the standard experiment set (Release build required):

```powershell
.\scripts\run_experiments.ps1 -Quick   # small sizes, checks the pipeline (~seconds)
.\scripts\run_experiments.ps1          # full run
```

This writes three files to `results/`:
- `timing.csv`: per-phase time
- `counts.csv`: per-phase counters
- `histograms.csv`: per-operation cost distributions

The runner sets High priority and pins the benchmark to a single core.

Or invoke the harness directly:

```powershell
.\out\build\x64-release\bench\dict_bench.exe --workload sorted,uniform --n 1000,100000 --reps 10
```

Run with `--help` to see all flags.

### Plots

```powershell
pip install -r scripts/requirements.txt
python scripts/plot_results.py
```

This writes PNGs to `results/plots/`: median ns/op vs n with a p5–p95 band, and per-operation cost CCDFs (tail plots).

## Implementing a module

1. Branch from `main`, e.g. `feature/red-black-tree` or `feature/randomized`.
2. Implement every method in your header. The interface is documented in `core/include/dict/ordered_map.hpp`.
3. Bump counters with `DICT_COUNT(counters_, field, 1)`. `counters.hpp` and the comments at the top of your header say what each field should count.
4. Implement `check_invariants()` properly. The tests are only as strong as it is.
5. Set `implemented = true`, then run `ctest --preset x64-debug` until green.
6. The randomized structure must draw all randomness from its seeded `rng_`, so runs are reproducible.
7. Once the randomized structure is chosen, give it its real name everywhere. The placeholder name appears in:
   - the folder `randomized/` and the header `randomized/include/dict/randomized_map.hpp`
   - the class `RandomizedMap`, its `name` string, and the CMake target `dict::randomized`
   - the `randomized` test suite in `tests/CMakeLists.txt`
   - the type alias and `--impl` default list in `bench/bench_main.cpp`
   - this README

   Searching the repo for `randomized` finds them all.

## AI use

See the report's AI-use section. The initial scaffold (build system, interface, test and benchmark harness) was generated with Claude Code and reviewed by the group.
