"""Simulates the skip list *model* to check the theory without the C++ code.

This is not the C++ skip list. It draws node heights (Geometric(1/2), capped
at 32), then works out what a search must compare against at each level:

  - start at the head on the top occupied level
  - move right while the next key is strictly less than the target
  - one more comparison against the node that stops the move (if any)
  - drop a level and repeat down to level 0

Searches never stop early, matching the C++ search. Targets fall in the gaps
between keys, so every search behaves like a miss (a hit differs only in the
last comparison).

Two questions:

  distinct  Does a search really read ((1 - p^2)/p) * log_{1/p} n = 1.5 log2 n
            distinct nodes, or does the model also give about 1.4 log2 n, as
            measured? If the model agrees with the measurement, the gap is in
            the formula, not in the implementation.

  seeds     Theory 6, revised: the cost of one structure is set mostly by the
            few nodes on its top levels, so the spread across seeds does not
            shrink as n grows. For many seeds per n this records the mean
            comparisons per search and the node counts on the top levels, and
            fits how much of the spread across seeds those counts explain.

Usage:
  python scripts/simulate_skiplist.py [results_dir] [--quick]

Writes results/summary/sim_distinct.csv, sim_seeds.csv and sim_seeds_explained.csv,
and results/plots/sim_distinct.png and sim_seeds.png. The full run takes a few minutes.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

MAX_LEVEL = 32
P = 0.5


def heights(n: int, rng: np.random.Generator) -> np.ndarray:
    return np.minimum(rng.geometric(1 - P, size=n), MAX_LEVEL)


def search_costs(h: np.ndarray, targets: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Comparisons and distinct nodes compared, per search.

    Node i holds key 2i, so target 2t+1 lies between nodes t and t+1. At each
    level the nodes present are those with height > level, in key order.
    """
    top = int(h.max())
    levels = [np.flatnonzero(h > lv) for lv in range(top)]
    comps = np.zeros(len(targets), dtype=np.int64)
    distinct = np.zeros(len(targets), dtype=np.int64)
    for j, t in enumerate(targets):
        pos = -1  # index of the current node; -1 is the head
        seen: set[int] = set()
        c = 0
        for lv in range(top - 1, -1, -1):
            idx = levels[lv]
            # Nodes at this level after the current one and before the target.
            lo = np.searchsorted(idx, pos, side="right")
            hi = np.searchsorted(idx, t, side="right")  # nodes with index <= t have key < target
            for k in idx[lo:hi]:
                c += 1
                seen.add(int(k))
            if hi < len(idx):  # the node that stops the move
                c += 1
                seen.add(int(idx[hi]))
            if hi > lo:
                pos = int(idx[hi - 1])
        comps[j] = c
        distinct[j] = len(seen)
    return comps, distinct


def run_distinct(sizes: list[int], seeds: int, searches: int) -> pd.DataFrame:
    rows = []
    for n in sizes:
        for s in range(seeds):
            rng = np.random.default_rng(10_000 + s)
            h = heights(n, rng)
            targets = rng.integers(0, n, size=searches)
            c, d = search_costs(h, targets)
            rows.append({"n": n, "seed": s, "comparisons": c.mean(), "distinct": d.mean()})
    df = pd.DataFrame(rows)
    df["log2n"] = np.log2(df["n"])
    return df


def run_seeds(sizes: list[int], seeds: int, searches: int, top_k: int) -> pd.DataFrame:
    rows = []
    for n in sizes:
        for s in range(seeds):
            rng = np.random.default_rng(20_000 + s)
            h = heights(n, rng)
            targets = rng.integers(0, n, size=searches)
            c, _ = search_costs(h, targets)
            # Node counts on the levels around log2 n, where the top of the search happens.
            base = int(math.log2(n))
            counts = {f"count_log2n_minus_{k}": int((h > base - k).sum()) for k in range(top_k)}
            rows.append({"n": n, "seed": s, "mean_comparisons": c.mean(), "top": int(h.max()), **counts})
    return pd.DataFrame(rows)


def explained(df: pd.DataFrame) -> pd.DataFrame:
    """Per n: spread across seeds (p95/p5) and R^2 of mean cost on top-level counts."""
    rows = []
    for n, g in df.groupby("n"):
        y = g["mean_comparisons"].to_numpy()
        cols = [c for c in g.columns if c.startswith("count_log2n")] + ["top"]
        x = np.column_stack([np.ones(len(g))] + [g[c].to_numpy(dtype=float) for c in cols])
        beta, *_ = np.linalg.lstsq(x, y, rcond=None)
        resid = y - x @ beta
        r2 = 1 - resid.var() / y.var() if y.var() > 0 else float("nan")
        rows.append({"n": n, "seeds": len(g), "spread_p95_over_p5": np.percentile(y, 95) / np.percentile(y, 5),
                     "cv": y.std() / y.mean(), "r2_top_levels": r2})
    return pd.DataFrame(rows)


def main() -> None:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    quick = "--quick" in sys.argv
    root = Path(args[0]) if args else Path(__file__).resolve().parent.parent / "results"
    (root / "summary").mkdir(parents=True, exist_ok=True)
    (root / "plots").mkdir(parents=True, exist_ok=True)

    sizes = [1_000, 10_000] if quick else [1_000, 10_000, 100_000, 1_000_000]
    d = run_distinct(sizes, seeds=2 if quick else 5, searches=200 if quick else 2_000)
    d.to_csv(root / "summary" / "sim_distinct.csv", index=False)
    per = d.groupby("n")[["comparisons", "distinct", "log2n"]].mean()
    print("model, mean per search / log2 n")
    print((per[["comparisons", "distinct"]].div(per["log2n"], axis=0)).round(3))

    fig, ax = plt.subplots(figsize=(5.5, 3.8))
    ax.plot(per.index, per["comparisons"] / per["log2n"], marker="o", label="comparisons (model)")
    ax.plot(per.index, per["distinct"] / per["log2n"], marker="o", label="distinct nodes (model)")
    for y, text in [(2.0, "2 log2 n"), (1.5, "formula: 1.5 log2 n")]:
        ax.axhline(y, color="grey", linestyle=":", linewidth=1)
        ax.text(per.index.min(), y, text, va="bottom", fontsize=8, color="grey")
    ax.set_xscale("log")
    ax.set_xlabel("n")
    ax.set_ylabel("per search / log2 n")
    ax.set_title("Skip list model: comparisons and distinct nodes")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(root / "plots" / "sim_distinct.png", dpi=150)
    plt.close(fig)

    s = run_seeds(sizes, seeds=20 if quick else 200, searches=200 if quick else 1_000, top_k=4)
    s.to_csv(root / "summary" / "sim_seeds.csv", index=False)
    e = explained(s)
    e.to_csv(root / "summary" / "sim_seeds_explained.csv", index=False)
    print("\nmodel, spread of mean comparisons across seeds")
    print(e.round(3).to_string(index=False))

    fig, ax1 = plt.subplots(figsize=(5.5, 3.8))
    ax1.plot(e["n"], e["spread_p95_over_p5"], marker="o", color="tab:blue", label="spread p95/p5")
    ax1.set_xscale("log")
    ax1.set_xlabel("n")
    ax1.set_ylabel("spread across seeds (p95 / p5)", color="tab:blue")
    ax2 = ax1.twinx()
    ax2.plot(e["n"], e["r2_top_levels"], marker="s", color="tab:orange", label="RÃ‚Â² from top-level counts")
    ax2.set_ylim(0, 1)
    ax2.set_ylabel("share explained by top levels (RÃ‚Â²)", color="tab:orange")
    ax1.set_title("Skip list model: seed spread and top levels")
    fig.tight_layout()
    fig.savefig(root / "plots" / "sim_seeds.png", dpi=150)
    plt.close(fig)


if __name__ == "__main__":
    main()
