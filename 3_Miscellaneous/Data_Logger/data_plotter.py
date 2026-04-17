#!/usr/bin/env python3
"""
Zoë2 Power Board CSV Plotter
==============================
Reads a CSV log file produced by datalogger.py and generates plots.

Usage:
    python plotter.py logs/zoe2_log_20260416_142305.csv
    python plotter.py logs/zoe2_log_20260416_142305.csv --save       # save as PNG instead of showing
    python plotter.py logs/zoe2_log_20260416_142305.csv --save --dpi 200

Dependencies:
    pip install matplotlib pandas
"""

import argparse
import sys
from pathlib import Path

try:
    import pandas as pd
except ImportError:
    print("ERROR: pandas not installed. Run: pip install pandas")
    sys.exit(1)

try:
    import matplotlib.pyplot as plt
    import matplotlib.dates as mdates
except ImportError:
    print("ERROR: matplotlib not installed. Run: pip install matplotlib")
    sys.exit(1)


def load_csv(path):
    """Load and validate a datalogger CSV file."""
    df = pd.read_csv(path)

    required = ["elapsed_s", "wiper", "power_in_W", "v_in", "i_in", "v_out", "i_out"]
    missing = [c for c in required if c not in df.columns]
    if missing:
        print(f"ERROR: Missing columns: {missing}")
        sys.exit(1)

    # Parse timestamp if present
    if "timestamp" in df.columns:
        df["timestamp"] = pd.to_datetime(df["timestamp"], errors="coerce")

    return df


def plot(df, title="Zoë2 Power Board", save_path=None, dpi=150):
    """Generate 4-panel plot from datalogger CSV data."""
    t = df["elapsed_s"]

    fig, axes = plt.subplots(2, 2, figsize=(14, 8))
    fig.suptitle(title, fontsize=14, fontweight="bold")

    # ── Panel 0: Power + Wiper ────────────────────────────────────────
    ax = axes[0, 0]
    ax.set_title("Input Power & Wiper Position")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Power (W)", color="tab:blue")
    ax.plot(t, df["power_in_W"], color="tab:blue", linewidth=1, label="Pin (W)")
    ax.tick_params(axis="y", labelcolor="tab:blue")

    ax2 = ax.twinx()
    ax2.set_ylabel("Wiper", color="tab:orange")
    ax2.plot(t, df["wiper"], color="tab:orange", linewidth=1, alpha=0.8, label="Wiper")
    ax2.tick_params(axis="y", labelcolor="tab:orange")

    # Combined legend
    lines_1, labels_1 = ax.get_legend_handles_labels()
    lines_2, labels_2 = ax2.get_legend_handles_labels()
    ax.legend(lines_1 + lines_2, labels_1 + labels_2, loc="upper left", fontsize=8)

    # ── Panel 1: Voltages ─────────────────────────────────────────────
    ax = axes[0, 1]
    ax.set_title("Voltages")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Voltage (V)")
    ax.plot(t, df["v_in"], color="tab:blue", linewidth=1, label="Vin")
    ax.plot(t, df["v_out"], color="tab:red", linewidth=1, label="Vout")
    ax.legend(loc="upper left", fontsize=8)
    ax.grid(True, alpha=0.3)

    # ── Panel 2: Currents ─────────────────────────────────────────────
    ax = axes[1, 0]
    ax.set_title("Currents")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Current (A)")
    ax.plot(t, df["i_in"], color="tab:blue", linewidth=1, label="Iin")
    ax.plot(t, df["i_out"], color="tab:red", linewidth=1, label="Iout")
    ax.legend(loc="upper left", fontsize=8)
    ax.grid(True, alpha=0.3)

    # ── Panel 3: Temperatures ─────────────────────────────────────────
    ax = axes[1, 1]
    ax.set_title("Thermocouple Temperatures")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Temperature (°C)")

    has_tc = False
    for col, label, color in [("tc1", "TC1", "tab:blue"),
                               ("tc2", "TC2", "tab:orange"),
                               ("tc3", "TC3", "tab:green")]:
        if col in df.columns and df[col].notna().any():
            ax.plot(t, df[col], color=color, linewidth=1, label=label)
            has_tc = True

    if has_tc:
        ax.legend(loc="upper left", fontsize=8)
        ax.grid(True, alpha=0.3)
    else:
        ax.text(0.5, 0.5, "No thermocouple data", transform=ax.transAxes,
                ha="center", va="center", fontsize=12, color="gray")

    fig.tight_layout(rect=[0, 0, 1, 0.95], h_pad=3, w_pad=3)

    # ── Stats annotation ──────────────────────────────────────────────
    duration = t.iloc[-1] - t.iloc[0] if len(t) > 1 else 0
    stats = (f"Samples: {len(df)}  |  Duration: {duration:.1f}s  |  "
             f"Avg Power: {df['power_in_W'].mean():.2f}W  |  "
             f"Avg Vin: {df['v_in'].mean():.2f}V")
    fig.text(0.5, 0.01, stats, ha="center", fontsize=9, color="gray")

    if save_path:
        fig.savefig(save_path, dpi=dpi, bbox_inches="tight")
        print(f"Saved plot to {save_path}")
    else:
        plt.show()


def main():
    ap = argparse.ArgumentParser(description="Plot Zoë2 datalogger CSV files")
    ap.add_argument("csvfile", type=str, help="Path to CSV log file")
    ap.add_argument("--save", action="store_true",
                    help="Save as PNG instead of showing interactively")
    ap.add_argument("--dpi", type=int, default=150,
                    help="DPI for saved image (default: 150)")
    ap.add_argument("--title", type=str, default=None,
                    help="Custom plot title")
    args = ap.parse_args()

    csv_path = Path(args.csvfile)
    if not csv_path.exists():
        print(f"ERROR: File not found: {csv_path}")
        sys.exit(1)

    print(f"Loading {csv_path}...")
    df = load_csv(csv_path)
    print(f"  {len(df)} samples loaded")

    title = args.title or f"Zoë2 Power Board — {csv_path.stem}"
    save_path = csv_path.with_suffix(".png") if args.save else None

    plot(df, title=title, save_path=save_path, dpi=args.dpi)


if __name__ == "__main__":
    main()