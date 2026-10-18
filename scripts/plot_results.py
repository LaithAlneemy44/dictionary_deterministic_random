"""Starter plots from the benchmark CSVs in results/.

    python scripts/plot_results.py            # reads results/, writes results/plots/

Produces:
  ns_per_op_<workload>_<phase>.png   median ns/op vs n per implementation, with a p5-p95 band
                                     (spread across seeds and repetitions)
  ccdf_<metric>_<workload>_<phase>_n<N>.png
                                     per-operation cost tail: P(cost >= x), log-scale y,
                                     pooled over all seeds, for the largest n

This is a starting point; extend it as the analysis needs.
"""

from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "results"
PLOTS = RESULTS / "plots"


def plot_timing(df: pd.DataFrame) -> None:
    for (workload, phase), g in df.groupby(["workload", "phase"]):
        fig, ax = plt.subplots(figsize=(6, 4))
        for impl, gi in g.groupby("impl"):
            s = gi.groupby("n")["ns_per_op"]
            med, lo, hi = s.median(), s.quantile(0.05), s.quantile(0.95)
            ax.plot(med.index, med.values, marker="o", label=impl)
            ax.fill_between(med.index, lo.values, hi.values, alpha=0.2)
        ax.set_xscale("log")
        ax.set_xlabel("n (keys)")
        ax.set_ylabel("ns / op")
        ax.set_title(f"{workload} - {phase}")
        ax.legend()
        fig.tight_layout()
        fig.savefig(PLOTS / f"ns_per_op_{workload}_{phase}.png", dpi=150)
        plt.close(fig)


def plot_ccdf(hist: pd.DataFrame) -> None:
    n_max = hist["n"].max()
    h = hist[hist["n"] == n_max]
    for (metric, workload, phase), g in h.groupby(["metric", "workload", "phase"]):
        fig, ax = plt.subplots(figsize=(6, 4))
        for impl, gi in g.groupby("impl"):
            counts = gi.groupby("value")["count"].sum().sort_index()
            # P(X >= x): reverse cumulative sum over the sorted values.
            ccdf = counts[::-1].cumsum()[::-1] / counts.sum()
            ax.step(ccdf.index, ccdf.values, where="post", label=impl)
        ax.set_yscale("log")
        ax.set_xlabel(f"{metric} per operation")
        ax.set_ylabel("P(cost >= x)")
        ax.set_title(f"{workload} - {phase}, n={n_max}")
        ax.legend()
        fig.tight_layout()
        fig.savefig(PLOTS / f"ccdf_{metric}_{workload}_{phase}_n{n_max}.png", dpi=150)
        plt.close(fig)


def main() -> None:
    PLOTS.mkdir(parents=True, exist_ok=True)
    timing = RESULTS / "timing.csv"
    hist = RESULTS / "histograms.csv"
    if timing.exists():
        plot_timing(pd.read_csv(timing))
    if hist.exists():
        plot_ccdf(pd.read_csv(hist))
    print(f"plots written to {PLOTS}")


if __name__ == "__main__":
    main()
