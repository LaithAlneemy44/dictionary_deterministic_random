"""Plots and summary tables from the benchmark CSVs in results/.

    python scripts/plot_results.py            # reads results/, writes results/plots/ and results/summary/
    python scripts/plot_results.py DIR        # same, for another results directory

Inputs (written by scripts/run_experiments.ps1; every group's file is read):
  timing_*.csv        wall-clock per phase (timing build)
  latency_*.csv       per-operation latency percentiles (timing build, --latency-out)
  latency_hist_*.csv  per-operation latency histograms, log bins
  counts_*.csv        structural counters per phase (instrumented build)
  histograms_*.csv    per-operation counter histograms (probe-key runs add distinct_keys)
  memory_*.csv        heap held by each structure after its insert phase
  seeds_*.csv         seed-variance runs (H4)

Plots (results/plots/):
  time_<workload>.png               median ns/op vs n, p5-p95 band; rows = phases, columns = key types
  ratio_<workload>.png              skip_list / red_black_tree median ns/op vs n
  memory.png                        bytes per key vs n (solid: requested, dashed: incl. allocator overhead)
  latency_<workload>_<key>.png      per-op latency p50 / p99 / p99.9 / p99.99 vs n
  ccdf_latency_<workload>_<key>.png per-op latency tail P(latency >= x) at the largest well-sampled n
  ccdf_counts_<workload>_<key>.png  per-op comparisons / node visits / structural tail at the largest n
  restructuring_by_workload.png     structural work and recolours per insert, by workload (H3)
  seeds_<metric>.png                skip list spread across algorithm seeds vs across input seeds (H4)
  seeds_spread_vs_n.png             those spreads as n grows (H4)
  path_length.png                   lookup cost / log2 n vs n, with the theoretical slopes (H2)
  tail_ratio.png                    p99 / p50 and p99.9 / p50 of lookup comparisons vs n (H2)
  ratio_sources.png                 skip / tree time ratio against the comparison, visit and distinct-key ratios (H1)
  write_cost.png                    time of a write minus its matching lookup, steady workload (H5)
  zipf_order.png                    zipf vs zipf_shuffled lookups: time and node visits

Summary tables (results/summary/): timing, latency, memory, count percentiles, seed variance, write cost.
"""

import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parent.parent
RESULTS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "results"
PLOTS = RESULTS / "plots"
SUMMARY = RESULTS / "summary"

IMPLS = ["red_black_tree", "skip_list", "std_map"]
COLOURS = {"red_black_tree": "#c0392b", "skip_list": "#2471a3", "std_map": "#7f8c8d"}
STYLES = {"red_black_tree": "-", "skip_list": "-", "std_map": "--"}
KEYS = ["int", "small_string", "large_string"]
PHASE_ORDER = ["insert", "find", "erase", "churn", "find_miss", "insert_new", "find_new", "erase_new"]
PERCENTILES = [("p50_ns", "p50"), ("p99_ns", "p99"), ("p999_ns", "p99.9"), ("p9999_ns", "p99.99")]


def load(pattern: str, exclude: str | None = None) -> pd.DataFrame | None:
    files = sorted(f for f in RESULTS.glob(pattern) if not (exclude and f.name.startswith(exclude)))
    frames = [pd.read_csv(f) for f in files if f.stat().st_size > 0]
    frames = [f for f in frames if not f.empty]
    return pd.concat(frames, ignore_index=True) if frames else None


def ordered(values, order):
    present = list(dict.fromkeys(values))
    return [v for v in order if v in present] + [v for v in present if v not in order]


def impl_line(ax, impl, x, y, **kw):
    ax.plot(x, y, color=COLOURS.get(impl), linestyle=STYLES.get(impl, "-"), marker="o", markersize=3,
            label=impl, **kw)


def finish(fig, path: Path):
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def hist_quantile(values: np.ndarray, counts: np.ndarray, q: float) -> float:
    order = np.argsort(values)
    v, c = values[order], counts[order]
    cum = np.cumsum(c)
    idx = np.searchsorted(cum, q * cum[-1], side="left")
    return float(v[min(idx, len(v) - 1)])


# ---- Timing ------------------------------------------------------------------

def plot_timing(df: pd.DataFrame) -> None:
    rows = []
    for workload, gw in df.groupby("workload"):
        phases = ordered(gw["phase"], PHASE_ORDER)
        keys = ordered(gw["key"], KEYS)
        fig, axes = plt.subplots(len(phases), len(keys), figsize=(4.2 * len(keys), 3.2 * len(phases)),
                                 squeeze=False, sharex=True)
        for i, phase in enumerate(phases):
            for j, key in enumerate(keys):
                ax = axes[i][j]
                g = gw[(gw["phase"] == phase) & (gw["key"] == key)]
                for impl in ordered(g["impl"], IMPLS):
                    s = g[g["impl"] == impl].groupby("n")["ns_per_op"]
                    med, lo, hi = s.median(), s.quantile(0.05), s.quantile(0.95)
                    impl_line(ax, impl, med.index, med.values)
                    ax.fill_between(med.index, lo.values, hi.values, color=COLOURS.get(impl), alpha=0.15)
                    for n in med.index:
                        rows.append({"impl": impl, "key": key, "workload": workload, "phase": phase, "n": n,
                                     "median_ns_per_op": med[n], "p5": lo[n], "p95": hi[n],
                                     "samples": int(s.count()[n])})
                ax.set_xscale("log")
                ax.set_title(f"{phase}, {key}")
                ax.set_ylabel("ns / op")
                ax.grid(alpha=0.3)
                if i == len(phases) - 1:
                    ax.set_xlabel("n (keys)")
        axes[0][0].legend()
        fig.suptitle(f"Time per operation: {workload} (median, p5-p95 band over seeds and reps)")
        finish(fig, PLOTS / f"time_{workload}.png")
    pd.DataFrame(rows).to_csv(SUMMARY / "timing.csv", index=False)


def plot_ratio(df: pd.DataFrame) -> None:
    med = df.groupby(["workload", "key", "phase", "impl", "n"])["ns_per_op"].median().unstack("impl")
    if not {"skip_list", "red_black_tree"} <= set(med.columns):
        return
    med["ratio"] = med["skip_list"] / med["red_black_tree"]
    for workload, gw in med.groupby(level="workload"):
        keys = ordered(gw.index.get_level_values("key"), KEYS)
        fig, axes = plt.subplots(1, len(keys), figsize=(4.2 * len(keys), 3.4), squeeze=False, sharey=True)
        for j, key in enumerate(keys):
            ax = axes[0][j]
            gk = gw.xs(key, level="key")
            for phase in ordered(gk.index.get_level_values("phase"), PHASE_ORDER):
                r = gk.xs(phase, level="phase")["ratio"].droplevel("workload")
                ax.plot(r.index, r.values, marker="o", markersize=3, label=phase)
            ax.axhline(1.0, color="black", linewidth=0.8)
            ax.set_xscale("log")
            ax.set_title(key)
            ax.set_xlabel("n (keys)")
            ax.grid(alpha=0.3)
        axes[0][0].set_ylabel("skip_list / red_black_tree (median ns/op)")
        axes[0][0].legend()
        fig.suptitle(f"Relative time: {workload} (above 1 = skip list slower)")
        finish(fig, PLOTS / f"ratio_{workload}.png")


# ---- Memory ------------------------------------------------------------------

def plot_memory(df: pd.DataFrame) -> None:
    df = df[df["workload"] == "uniform"].copy()
    df["bytes_per_key"] = df["heap_bytes"] / df["size"]
    df["with_overhead_per_key"] = (df["heap_bytes"] + df["heap_overhead"]) / df["size"]
    df["blocks_per_key"] = df["heap_blocks"] / df["size"]
    summary = (df.groupby(["impl", "key", "n"])[["bytes_per_key", "with_overhead_per_key", "blocks_per_key"]]
               .median().reset_index())
    summary.to_csv(SUMMARY / "memory.csv", index=False)

    keys = ordered(summary["key"], KEYS)
    fig, axes = plt.subplots(1, len(keys), figsize=(4.2 * len(keys), 3.6), squeeze=False)
    for j, key in enumerate(keys):
        ax = axes[0][j]
        g = summary[summary["key"] == key]
        for impl in ordered(g["impl"], IMPLS):
            gi = g[g["impl"] == impl].sort_values("n")
            impl_line(ax, impl, gi["n"], gi["bytes_per_key"])
            ax.plot(gi["n"], gi["with_overhead_per_key"], color=COLOURS.get(impl), linestyle=":", linewidth=1)
        ax.set_xscale("log")
        ax.set_title(key)
        ax.set_xlabel("n (keys)")
        ax.set_ylabel("heap bytes per key")
        ax.set_ylim(bottom=0)
        ax.grid(alpha=0.3)
    axes[0][0].legend()
    fig.suptitle("Memory after inserting n keys (solid: requested bytes, dotted: incl. allocator overhead)")
    finish(fig, PLOTS / "memory.png")


# ---- Per-operation latency ---------------------------------------------------

def plot_latency(df: pd.DataFrame) -> None:
    cols = [c for c, _ in PERCENTILES] + ["mean_ns", "max_ns"]
    summary = df.groupby(["impl", "key", "workload", "phase", "n"])[cols].median().reset_index()
    summary.to_csv(SUMMARY / "latency.csv", index=False)
    for (workload, key), g in summary.groupby(["workload", "key"]):
        phases = ordered(g["phase"], PHASE_ORDER)
        fig, axes = plt.subplots(len(phases), len(PERCENTILES), figsize=(3.6 * len(PERCENTILES), 3.0 * len(phases)),
                                 squeeze=False, sharex=True)
        for i, phase in enumerate(phases):
            for j, (col, label) in enumerate(PERCENTILES):
                ax = axes[i][j]
                gp = g[g["phase"] == phase]
                for impl in ordered(gp["impl"], IMPLS):
                    gi = gp[gp["impl"] == impl].sort_values("n")
                    impl_line(ax, impl, gi["n"], gi[col])
                ax.set_xscale("log")
                ax.set_title(f"{phase}: {label}")
                ax.grid(alpha=0.3)
                if j == 0:
                    ax.set_ylabel("ns")
                if i == len(phases) - 1:
                    ax.set_xlabel("n (keys)")
        axes[0][0].legend()
        fig.suptitle(f"Per-operation latency percentiles: {workload}, {key} keys (median over seed pairs)")
        finish(fig, PLOTS / f"latency_{workload}_{key}.png")


def plot_latency_ccdf(hist: pd.DataFrame) -> None:
    for (workload, key), g in hist.groupby(["workload", "key"]):
        # Largest n that has more than one seed pair, else the largest n.
        pairs = g.groupby("n").apply(lambda x: x[["input_seed", "algo_seed"]].drop_duplicates().shape[0])
        multi = pairs[pairs > 1]
        n = int(multi.index.max() if not multi.empty else pairs.index.max())
        gn = g[g["n"] == n]
        phases = ordered(gn["phase"], PHASE_ORDER)
        fig, axes = plt.subplots(1, len(phases), figsize=(4.2 * len(phases), 3.6), squeeze=False, sharey=True)
        for j, phase in enumerate(phases):
            ax = axes[0][j]
            gp = gn[gn["phase"] == phase]
            for impl in ordered(gp["impl"], IMPLS):
                counts = gp[gp["impl"] == impl].groupby("bin_ns")["count"].sum().sort_index()
                ccdf = counts[::-1].cumsum()[::-1] / counts.sum()
                ax.step(ccdf.index, ccdf.values, where="post", color=COLOURS.get(impl),
                        linestyle=STYLES.get(impl, "-"), label=impl)
            for q in (1e-2, 1e-3, 1e-4):
                ax.axhline(q, color="black", linewidth=0.5, alpha=0.4)
            ax.set_xscale("log")
            ax.set_yscale("log")
            ax.set_title(phase)
            ax.set_xlabel("latency (ns)")
            ax.grid(alpha=0.3)
        axes[0][0].set_ylabel("P(latency >= x)")
        axes[0][0].legend()
        fig.suptitle(f"Per-operation latency tail: {workload}, {key} keys, n={n} (lines at worst 1%, 0.1%, 0.01%)")
        finish(fig, PLOTS / f"ccdf_latency_{workload}_{key}.png")


# ---- Counter distributions ---------------------------------------------------

def count_percentiles(hist: pd.DataFrame) -> pd.DataFrame:
    rows = []
    for keys_, g in hist.groupby(["impl", "key", "workload", "phase", "n", "metric"]):
        agg = g.groupby("value")["count"].sum()
        v, c = agg.index.to_numpy(dtype=float), agg.to_numpy(dtype=float)
        row = dict(zip(["impl", "key", "workload", "phase", "n", "metric"], keys_))
        row["mean"] = float((v * c).sum() / c.sum())
        for q, name in [(0.5, "p50"), (0.9, "p90"), (0.99, "p99"), (0.999, "p99.9"), (0.9999, "p99.99")]:
            row[name] = hist_quantile(v, c, q)
        row["max"] = float(v.max())
        rows.append(row)
    return pd.DataFrame(rows)


def plot_count_ccdf(hist: pd.DataFrame) -> None:
    metrics = ["comparisons", "node_visits", "structural"]
    for (workload, key), g in hist.groupby(["workload", "key"]):
        if key == "probe":  # same shapes as int; its distinct_keys is plotted in path_length.png
            continue
        n = int(g["n"].max())
        gn = g[g["n"] == n]
        phases = ordered(gn["phase"], PHASE_ORDER)
        fig, axes = plt.subplots(len(phases), len(metrics), figsize=(4.0 * len(metrics), 3.0 * len(phases)),
                                 squeeze=False)
        for i, phase in enumerate(phases):
            for j, metric in enumerate(metrics):
                ax = axes[i][j]
                gp = gn[(gn["phase"] == phase) & (gn["metric"] == metric)]
                for impl in ordered(gp["impl"], IMPLS):
                    counts = gp[gp["impl"] == impl].groupby("value")["count"].sum().sort_index()
                    if counts.empty or counts.index.max() == 0:
                        continue
                    ccdf = counts[::-1].cumsum()[::-1] / counts.sum()
                    ax.step(ccdf.index, ccdf.values, where="post", color=COLOURS.get(impl),
                            linestyle=STYLES.get(impl, "-"), label=impl)
                ax.set_yscale("log")
                ax.set_title(f"{phase}: {metric}")
                ax.grid(alpha=0.3)
                if j == 0:
                    ax.set_ylabel("P(cost >= x)")
        axes[0][0].legend()
        fig.suptitle(f"Per-operation cost tail: {workload}, {key} keys, n={n}")
        finish(fig, PLOTS / f"ccdf_counts_{workload}_{key}.png")


def plot_restructuring(counts: pd.DataFrame) -> None:
    c = counts[(counts["key"] == "int") & (counts["phase"].isin(["insert", "churn"]))]
    if c.empty:
        return
    n = int(c["n"].max())
    c = c[(c["n"] == n) & (c["impl"].isin(["red_black_tree", "skip_list"]))].copy()
    c["structural_per_op"] = c["structural"] / c["ops"]
    c["recolors_per_op"] = c["recolors"] / c["ops"]
    agg = c.groupby(["workload", "phase", "impl"])[["structural_per_op", "recolors_per_op"]].median().reset_index()
    agg.to_csv(SUMMARY / "restructuring.csv", index=False)

    labels = [f"{w}\n{p}" for w, p in agg[["workload", "phase"]].drop_duplicates().itertuples(index=False)]
    groups = agg[["workload", "phase"]].drop_duplicates().itertuples(index=False)
    groups = list(groups)
    x = np.arange(len(groups))
    fig, ax = plt.subplots(figsize=(max(6, 1.4 * len(groups)), 3.8))
    width = 0.27
    series = [("red_black_tree", "structural_per_op", "red-black tree: rotations"),
              ("red_black_tree", "recolors_per_op", "red-black tree: recolours"),
              ("skip_list", "structural_per_op", "skip list: splices")]
    for k, (impl, col, label) in enumerate(series):
        vals = [agg[(agg["workload"] == w) & (agg["phase"] == p) & (agg["impl"] == impl)][col].median()
                for w, p in groups]
        ax.bar(x + (k - 1) * width, vals, width, label=label, color=COLOURS[impl], alpha=1.0 - 0.35 * (k == 1))
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.set_ylabel("per operation")
    ax.set_title(f"Restructuring work per insert, int keys, n={n}")
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    finish(fig, PLOTS / "restructuring_by_workload.png")


# ---- Seed variance (H4) ------------------------------------------------------

def plot_seed_variance(df: pd.DataFrame, value: str, label: str, name: str) -> pd.DataFrame:
    rows = []
    s = df[df["impl"] == "skip_list"]
    phases = ordered(s["phase"], PHASE_ORDER)
    spread = lambda arrs: float(np.median([np.percentile(a, 95) / np.percentile(a, 5) for a in arrs if len(a) > 1]))
    for (n, phase), gp in s.groupby(["n", "phase"]):
        g = gp.groupby(["input_seed", "algo_seed"])[value].median().reset_index()
        by_input = [grp[value].to_numpy() for _, grp in g.groupby("input_seed")]  # spread over algo seeds
        by_algo = [grp[value].to_numpy() for _, grp in g.groupby("algo_seed")]    # spread over input seeds
        rows.append({"metric": value, "phase": phase, "n": n,
                     "spread_across_algo_seeds_p95_over_p5": spread(by_input),
                     "spread_across_input_seeds_p95_over_p5": spread(by_algo)})

    # Boxes at the largest n.
    n_max = int(s["n"].max())
    s = s[s["n"] == n_max]
    fig, axes = plt.subplots(1, len(phases), figsize=(4.4 * len(phases), 3.8), squeeze=False)
    for j, phase in enumerate(phases):
        g = s[s["phase"] == phase].groupby(["input_seed", "algo_seed"])[value].median().reset_index()
        by_input = [grp[value].to_numpy() for _, grp in g.groupby("input_seed")]
        ax = axes[0][j]
        ax.boxplot(by_input, positions=np.arange(len(by_input)), widths=0.6)
        ax.set_xticks(np.arange(len(by_input)))
        ax.set_xticklabels([str(i) for i in sorted(g["input_seed"].unique())], rotation=45)
        ax.set_xlabel("input seed (box = spread over algorithm seeds)")
        ax.set_title(phase)
        ax.grid(alpha=0.3)
        if j == 0:
            ax.set_ylabel(label)
    fig.suptitle(f"Skip list: variation from its own randomness, per input ({name}, n={n_max})")
    finish(fig, PLOTS / f"seeds_{name}.png")
    return pd.DataFrame(rows)


def plot_seed_spread_vs_n(variance: pd.DataFrame) -> None:
    metrics = list(dict.fromkeys(variance["metric"]))
    fig, axes = plt.subplots(1, len(metrics), figsize=(5.0 * len(metrics), 3.8), squeeze=False)
    for j, metric in enumerate(metrics):
        ax = axes[0][j]
        g = variance[variance["metric"] == metric]
        for k, phase in enumerate(ordered(g["phase"], PHASE_ORDER)):
            gp = g[g["phase"] == phase].sort_values("n")
            colour = f"C{k}"
            ax.plot(gp["n"], gp["spread_across_algo_seeds_p95_over_p5"], color=colour, marker="o", markersize=3,
                    label=f"{phase}: algorithm seeds")
            ax.plot(gp["n"], gp["spread_across_input_seeds_p95_over_p5"], color=colour, marker="o", markersize=3,
                    linestyle="--", label=f"{phase}: input seeds")
        ax.axhline(1.0, color="black", linewidth=0.8)
        ax.set_xscale("log")
        ax.set_xlabel("n (keys)")
        ax.set_ylabel("p95 / p5 across seeds")
        ax.set_title(metric)
        ax.grid(alpha=0.3)
        ax.legend(fontsize=7)
    fig.suptitle("Skip list: spread across its own seeds (solid) vs across inputs (dashed)")
    finish(fig, PLOTS / "seeds_spread_vs_n.png")


# ---- Explanations for the hypotheses -------------------------------------------

def theory_line(ax, y: float, text: str) -> None:
    ax.axhline(y, color="black", linestyle=":", linewidth=0.9)
    ax.text(0.02, y, text, transform=ax.get_yaxis_transform(), fontsize=7, va="bottom")


def plot_path_length(cp: pd.DataFrame) -> None:
    """Lookup cost divided by log2 n. A flat line at height c means cost = c * log2 n.

    Skip list with p = 1/2: about (1/p) log_{1/p} n = 2 log2 n steps and comparisons. A
    step after dropping a level compares against the same node again with probability p,
    so it reads about (1 - p^2)/p * log_{1/p} n = 1.5 log2 n distinct nodes. A balanced
    search tree reads about log2 n.
    """
    f = cp[(cp["workload"] == "uniform") & (cp["phase"] == "find")]
    panels = [("comparisons", "int", "key comparisons"), ("node_visits", "int", "node visits (steps)"),
              ("distinct_keys", "probe", "distinct keys read")]
    theory = {"comparisons": [(2.0, "skip list: 2 log2 n")],
              "node_visits": [(2.0, "skip list: 2 log2 n"), (1.0, "balanced tree: log2 n")],
              "distinct_keys": [(1.5, "skip list: 1.5 log2 n"), (1.0, "balanced tree: log2 n")]}
    panels = [p for p in panels if not f[(f["metric"] == p[0]) & (f["key"] == p[1])].empty]
    if not panels:
        return
    fig, axes = plt.subplots(1, len(panels), figsize=(4.4 * len(panels), 3.8), squeeze=False, sharey=True)
    for j, (metric, key, title) in enumerate(panels):
        ax = axes[0][j]
        g = f[(f["metric"] == metric) & (f["key"] == key)]
        for impl in ordered(g["impl"], IMPLS):
            gi = g[g["impl"] == impl].sort_values("n")
            if gi["mean"].max() == 0:  # std_map has no node_visits counter
                continue
            impl_line(ax, impl, gi["n"], gi["mean"] / np.log2(gi["n"]))
        for y, text in theory[metric]:
            theory_line(ax, y, text)
        ax.set_xscale("log")
        ax.set_ylim(bottom=0)
        ax.set_title(title)
        ax.set_xlabel("n (keys)")
        ax.grid(alpha=0.3)
    axes[0][0].set_ylabel("mean per lookup / log2 n")
    axes[0][0].legend(fontsize=8)
    fig.suptitle("Lookup path length, uniform find (dotted: theory)")
    finish(fig, PLOTS / "path_length.png")


def plot_tail_ratio(cp: pd.DataFrame) -> None:
    f = cp[(cp["workload"] == "uniform") & (cp["phase"] == "find") & (cp["key"] == "int")
           & (cp["metric"] == "comparisons")]
    if f.empty:
        return
    fig, axes = plt.subplots(1, 2, figsize=(8.8, 3.6), squeeze=False, sharey=True)
    for j, q in enumerate(["p99", "p99.9"]):
        ax = axes[0][j]
        for impl in ordered(f["impl"], IMPLS):
            gi = f[f["impl"] == impl].sort_values("n")
            impl_line(ax, impl, gi["n"], gi[q] / gi["p50"])
        ax.set_xscale("log")
        ax.set_title(f"{q} / p50")
        ax.set_xlabel("n (keys)")
        ax.grid(alpha=0.3)
    axes[0][0].set_ylabel("comparisons per lookup, tail / median")
    axes[0][0].legend()
    fig.suptitle("Relative tail of lookup cost, uniform int find")
    finish(fig, PLOTS / "tail_ratio.png")


def plot_ratio_sources(timing: pd.DataFrame, cp: pd.DataFrame) -> None:
    """Which count does the skip list / red-black tree time ratio follow?"""
    t = timing[(timing["workload"] == "uniform") & (timing["phase"] == "find")]
    tm = t.groupby(["key", "impl", "n"])["ns_per_op"].median().unstack("impl")
    if not {"skip_list", "red_black_tree"} <= set(tm.columns):
        return
    f = cp[(cp["workload"] == "uniform") & (cp["phase"] == "find")]
    fig, ax = plt.subplots(figsize=(6.6, 4.2))
    for key in ordered(tm.index.get_level_values("key"), KEYS):
        r = (tm.loc[key]["skip_list"] / tm.loc[key]["red_black_tree"]).dropna()
        ax.plot(r.index, r.values, marker="o", markersize=3, label=f"time, {key}")
    for metric, key, style in [("node_visits", "int", "--"), ("comparisons", "int", "-."),
                               ("distinct_keys", "probe", ":")]:
        g = f[(f["metric"] == metric) & (f["key"] == key)].pivot_table(index="n", columns="impl", values="mean")
        if {"skip_list", "red_black_tree"} <= set(g.columns):
            r = g["skip_list"] / g["red_black_tree"]
            ax.plot(r.index, r.values, color="black", linestyle=style, label=f"{metric.replace('_', ' ')} ratio")
    ax.axhline(1.0, color="black", linewidth=0.8)
    ax.set_xscale("log")
    ax.set_xlabel("n (keys)")
    ax.set_ylabel("skip_list / red_black_tree")
    ax.grid(alpha=0.3)
    ax.legend(fontsize=8)
    ax.set_title("Uniform find: time ratio against count ratios")
    finish(fig, PLOTS / "ratio_sources.png")


def plot_write_cost(timing: pd.DataFrame) -> None:
    """steady workload: each write retraces the search path of a matching lookup, so the
    difference is the cost of the write itself (allocation or free, linking, rebalancing)."""
    s = timing[(timing["workload"] == "steady") & (timing["key"] == "int")]
    med = s.groupby(["impl", "phase", "n"])["ns_per_op"].median().unstack("phase")
    if not {"find_miss", "insert_new", "find_new", "erase_new"} <= set(med.columns):
        return
    med["insert_extra"] = med["insert_new"] - med["find_miss"]
    med["erase_extra"] = med["erase_new"] - med["find_new"]
    med.reset_index().to_csv(SUMMARY / "write_cost.csv", index=False)

    panels = [("find_miss", "search: lookup that misses"), ("insert_extra", "insert minus matching miss"),
              ("erase_extra", "erase minus matching hit")]
    fig, axes = plt.subplots(1, len(panels), figsize=(4.4 * len(panels), 3.6), squeeze=False)
    for j, (col, title) in enumerate(panels):
        ax = axes[0][j]
        for impl in ordered(med.index.get_level_values("impl"), IMPLS):
            r = med.xs(impl, level="impl")[col]
            impl_line(ax, impl, r.index, r.values)
        ax.axhline(0.0, color="black", linewidth=0.8)
        ax.set_xscale("log")
        ax.set_title(title)
        ax.set_xlabel("n (keys)")
        ax.set_ylabel("ns / op")
        ax.grid(alpha=0.3)
    axes[0][0].legend()
    fig.suptitle("Cost of a write beyond its search, steady workload, int keys (median)")
    finish(fig, PLOTS / "write_cost.png")


def plot_zipf_order(timing: pd.DataFrame, cp: pd.DataFrame | None) -> None:
    families = ["zipf", "zipf_shuffled"]
    t = timing[timing["workload"].isin(families) & (timing["key"] == "int") & (timing["phase"] == "find")]
    if t["workload"].nunique() < len(families):
        return
    c = pd.DataFrame()
    if cp is not None:
        c = cp[cp["workload"].isin(families) & (cp["key"] == "int") & (cp["phase"] == "find")
               & (cp["metric"] == "node_visits")]
    rows = 1 if c.empty else 2
    fig, axes = plt.subplots(rows, len(families), figsize=(4.6 * len(families), 3.4 * rows), squeeze=False,
                             sharey="row")
    for j, family in enumerate(families):
        tm = t[t["workload"] == family].groupby(["impl", "n"])["ns_per_op"].median()
        ax = axes[0][j]
        for impl in ordered(tm.index.get_level_values("impl"), IMPLS):
            r = tm.xs(impl, level="impl")
            impl_line(ax, impl, r.index, r.values)
        ax.set_title(f"{family}: time")
        ax.set_ylabel("ns / lookup")
        if rows > 1:
            ax = axes[1][j]
            cf = c[c["workload"] == family]
            for impl in ordered(cf["impl"], IMPLS):
                gi = cf[cf["impl"] == impl].sort_values("n")
                if gi["mean"].max() == 0:
                    continue
                impl_line(ax, impl, gi["n"], gi["mean"])
            ax.set_title(f"{family}: node visits")
            ax.set_ylabel("mean per lookup")
        for i in range(rows):
            axes[i][j].set_xscale("log")
            axes[i][j].set_xlabel("n (keys)")
            axes[i][j].grid(alpha=0.3)
    axes[0][0].legend()
    fig.suptitle("Zipf lookups: hot keys are the oldest (zipf) vs unrelated to insertion order (zipf_shuffled)")
    finish(fig, PLOTS / "zipf_order.png")


def main() -> None:
    PLOTS.mkdir(parents=True, exist_ok=True)
    SUMMARY.mkdir(parents=True, exist_ok=True)

    timing = load("timing_*.csv")
    if timing is not None:
        plot_timing(timing)
        plot_ratio(timing)

    memory = load("memory_*.csv")
    if memory is not None:
        plot_memory(memory)

    latency = load("latency_*.csv", exclude="latency_hist_")
    if latency is not None:
        plot_latency(latency)
    latency_hist = load("latency_hist_*.csv")
    if latency_hist is not None:
        plot_latency_ccdf(latency_hist)

    hist = load("histograms_*.csv")
    cp = None
    if hist is not None:
        cp = count_percentiles(hist)
        cp.to_csv(SUMMARY / "count_percentiles.csv", index=False)
        plot_count_ccdf(hist)
        plot_path_length(cp)
        plot_tail_ratio(cp)
    counts = load("counts_*.csv")
    if counts is not None:
        plot_restructuring(counts)

    if timing is not None:
        plot_write_cost(timing)
        plot_zipf_order(timing, cp)
        if cp is not None:
            plot_ratio_sources(timing, cp)

    variance = []
    seeds_timing = load("seeds_timing.csv")
    if seeds_timing is not None:
        variance.append(plot_seed_variance(seeds_timing, "ns_per_op", "ns / op", "time"))
    seeds_counts = load("seeds_counts.csv")
    if seeds_counts is not None:
        seeds_counts = seeds_counts.assign(node_visits_per_op=seeds_counts["node_visits"] / seeds_counts["ops"])
        variance.append(plot_seed_variance(seeds_counts, "node_visits_per_op", "node visits / op", "node_visits"))
    if variance:
        variance = pd.concat(variance)
        variance.to_csv(SUMMARY / "seed_variance.csv", index=False)
        plot_seed_spread_vs_n(variance)

    print(f"plots written to {PLOTS}\nsummaries written to {SUMMARY}")


if __name__ == "__main__":
    main()
