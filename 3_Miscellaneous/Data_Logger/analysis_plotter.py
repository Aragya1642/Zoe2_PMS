#!/usr/bin/env python3
"""
Zoë2 Power Board — Efficiency Plotter
=======================================
Plots converter efficiency (P_out / P_in) with P_in and P_out overlay.

Usage:
    python efficiency_plotter.py logs/zoe2_log.csv
    python efficiency_plotter.py logs/zoe2_log.csv --save
    python efficiency_plotter.py logs/zoe2_log.csv --save --tstart 10 --tend 42

Dependencies:
    pip install matplotlib pandas numpy
"""

import argparse
import sys
from pathlib import Path

try:
    import pandas as pd
    import numpy as np
except ImportError:
    print("ERROR: Install dependencies: pip install pandas numpy")
    sys.exit(1)

try:
    import matplotlib.pyplot as plt
except ImportError:
    print("ERROR: Install matplotlib: pip install matplotlib")
    sys.exit(1)


STATE_COLORS = {0: "#cccccc", 1: "#c8e6c9", 2: "#fff9c4", 3: "#ffcdd2"}
STATE_NAMES  = {0: "IDLE", 1: "TRACKING", 2: "SCAN", 3: "FAULT"}


def load_csv(path):
    df = pd.read_csv(path)
    required = ["elapsed_s", "v_in", "i_in", "v_out", "i_out"]
    missing = [c for c in required if c not in df.columns]
    if missing:
        print(f"ERROR: Missing columns: {missing}")
        sys.exit(1)
    return df


def plot_efficiency(df, title="Zoë2 Converter Efficiency", save_path=None, dpi=150):
    t = df["elapsed_s"]
    p_in = df["v_in"] * df["i_in"]
    p_out = df["v_out"] * df["i_out"]
    eff = np.where(p_in > 0.5, p_out / p_in, np.nan)
    eff = np.clip(eff, 0, 1.5)
    eff_series = pd.Series(eff, index=df.index)

    fig, ax = plt.subplots(figsize=(12, 6))
    fig.suptitle(title, fontsize=14, fontweight="bold")

    # state bands
    if "state" in df.columns and df["state"].notna().any():
        states = df["state"].fillna(0).astype(int).values
        times = t.values
        start_idx = 0
        for i in range(1, len(states)):
            if states[i] != states[start_idx] or i == len(states) - 1:
                end_idx = i if states[i] != states[start_idx] else i + 1
                s = int(states[start_idx])
                ax.axvspan(times[start_idx], times[min(end_idx, len(times) - 1)],
                           alpha=0.15, color=STATE_COLORS.get(s, "#ccc"), zorder=0)
                start_idx = i

    # efficiency on left axis
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Efficiency", color="#0D47A1")
    ax.plot(t, eff, color="#0D47A1", linewidth=1.5, label="Efficiency")

    avg_eff = np.nanmean(eff)
    ax.axhline(y=avg_eff, color="#FF9800", linestyle="--", linewidth=1.5,
               label=f"Average ({avg_eff:.1%})")

    ax.set_ylim(0, max(np.nanmax(eff) * 1.1, 0.1) if np.any(np.isfinite(eff)) else 1.0)
    ax.axhline(y=1.0, color="gray", linestyle="--", alpha=0.3, linewidth=0.8)
    ax.tick_params(axis="y", labelcolor="#0D47A1")
    ax.grid(True, alpha=0.2)

    # power on right axis
    ax2 = ax.twinx()
    ax2.set_ylabel("Power (W)", color="#666666")
    ax2.plot(t, p_in, color="#E91E63", linewidth=1, alpha=0.5, label="P_in")
    ax2.plot(t, p_out, color="#4CAF50", linewidth=1, alpha=0.5, label="P_out")
    ax2.tick_params(axis="y", labelcolor="#666666")
    p_max = max(p_in.max(), p_out.max(), 1)
    ax2.set_ylim(0, p_max * 1.15)

    # combined legend
    lines_1, labels_1 = ax.get_legend_handles_labels()
    lines_2, labels_2 = ax2.get_legend_handles_labels()
    ax.legend(lines_1 + lines_2, labels_1 + labels_2, fontsize=8, loc="lower right")

    # stats box
    tracking_mask = df["state"] == 1 if "state" in df.columns else pd.Series(True, index=df.index)
    tracking_eff = eff_series[tracking_mask].dropna()
    if len(tracking_eff) > 0:
        ax.text(0.02, 0.95,
                f"Avg eff (tracking): {tracking_eff.mean():.1%}\n"
                f"Avg P_in: {p_in[tracking_mask].mean():.1f}W  "
                f"P_out: {p_out[tracking_mask].mean():.1f}W",
                transform=ax.transAxes, fontsize=9, va="top",
                bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.8))

    fig.tight_layout(rect=[0, 0, 1, 0.95])

    if save_path:
        fig.savefig(save_path, dpi=dpi, bbox_inches="tight")
        print(f"Saved to {save_path}")
    else:
        plt.show()


def main():
    ap = argparse.ArgumentParser(description="Zoë2 Efficiency Plotter")
    ap.add_argument("csvfile", type=str, help="Path to CSV log file")
    ap.add_argument("--save", action="store_true", help="Save as PNG")
    ap.add_argument("--dpi", type=int, default=150)
    ap.add_argument("--title", type=str, default=None)
    ap.add_argument("--tstart", type=float, default=None)
    ap.add_argument("--tend", type=float, default=None)
    args = ap.parse_args()

    csv_path = Path(args.csvfile)
    if not csv_path.exists():
        print(f"ERROR: File not found: {csv_path}")
        sys.exit(1)

    df = load_csv(csv_path)
    print(f"Loaded {len(df)} samples")

    if args.tstart is not None:
        df = df[df["elapsed_s"] >= args.tstart]
    if args.tend is not None:
        df = df[df["elapsed_s"] <= args.tend]
    if df.empty:
        print("ERROR: No data in range.")
        sys.exit(1)

    title = args.title or f"Zoë2 Efficiency — {csv_path.stem}"

    if args.save:
        out_dir = csv_path.parent / (csv_path.stem + "_panels")
        out_dir.mkdir(exist_ok=True)
        save_path = out_dir / "efficiency.png"
    else:
        save_path = None

    plot_efficiency(df, title=title, save_path=save_path, dpi=args.dpi)


if __name__ == "__main__":
    main()