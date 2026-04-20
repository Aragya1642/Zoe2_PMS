#!/usr/bin/env python3
"""
Zoë2 Power Board CSV Plotter
==============================
Reads a CSV log file produced by datalogger.py and generates plots.

Usage:
    python plotter.py logs/zoe2_log.csv
    python plotter.py logs/zoe2_log.csv --save                # combined 4-panel PNG
    python plotter.py logs/zoe2_log.csv --save-each           # each panel as separate PNG
    python plotter.py logs/zoe2_log.csv --save --save-each    # both
    python plotter.py logs/zoe2_log.csv --tstart 10 --tend 30

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

    if "timestamp" in df.columns:
        df["timestamp"] = pd.to_datetime(df["timestamp"], errors="coerce")

    return df


# ─────────────────────────────────────────────────────────────────────
# State band helper
# ─────────────────────────────────────────────────────────────────────

STATE_COLORS = {0: "#cccccc", 1: "#c8e6c9", 2: "#fff9c4", 3: "#ffcdd2"}
STATE_NAMES  = {0: "IDLE", 1: "TRACKING", 2: "SCAN", 3: "FAULT"}


def draw_state_bands(ax, df):
    """Draw colored background bands for MPPT state changes."""
    if "state" not in df.columns or df["state"].isna().all():
        return False
    states = df["state"].fillna(0).astype(int).values
    times = df["elapsed_s"].values
    start_idx = 0
    for i in range(1, len(states)):
        if states[i] != states[start_idx] or i == len(states) - 1:
            end_idx = i if states[i] != states[start_idx] else i + 1
            s = int(states[start_idx])
            ax.axvspan(times[start_idx], times[min(end_idx, len(times) - 1)],
                       alpha=0.3, color=STATE_COLORS.get(s, "#ccc"), zorder=0)
            start_idx = i
    return True


# ─────────────────────────────────────────────────────────────────────
# Individual panel functions
# ─────────────────────────────────────────────────────────────────────

def plot_power_wiper(ax, df):
    """Panel: Input Power & Wiper Position."""
    from matplotlib.patches import Patch

    t = df["elapsed_s"]
    ax.set_title("Input Power & Wiper Position")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Power (W)", color="tab:blue")
    ax.plot(t, df["power_in_W"], color="tab:blue", linewidth=1, label="Pin (W)")
    ax.tick_params(axis="y", labelcolor="tab:blue")

    ax2 = ax.twinx()
    ax2.set_ylabel("Wiper", color="tab:orange")
    ax2.plot(t, df["wiper"], color="tab:orange", linewidth=1, alpha=0.8, label="Wiper")
    ax2.tick_params(axis="y", labelcolor="tab:orange")

    has_state = draw_state_bands(ax, df)

    lines_1, labels_1 = ax.get_legend_handles_labels()
    lines_2, labels_2 = ax2.get_legend_handles_labels()
    legend_items = lines_1 + lines_2
    legend_labels = labels_1 + labels_2
    if has_state:
        seen_states = set(df["state"].dropna().astype(int).unique())
        for s, c in STATE_COLORS.items():
            if s in seen_states:
                legend_items.append(Patch(facecolor=c, alpha=0.3, label=STATE_NAMES[s]))
                legend_labels.append(STATE_NAMES[s])
    ax.legend(legend_items, legend_labels, loc="upper left", fontsize=7)


def plot_voltages(ax, df):
    """Panel: Voltages."""
    t = df["elapsed_s"]
    ax.set_title("Voltages")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Voltage (V)")
    ax.plot(t, df["v_in"], color="tab:blue", linewidth=1, label="Vin")
    ax.plot(t, df["v_out"], color="tab:red", linewidth=1, label="Vout")
    ax.legend(loc="upper left", fontsize=8)
    ax.grid(True, alpha=0.3)


def plot_currents(ax, df):
    """Panel: Currents."""
    t = df["elapsed_s"]
    ax.set_title("Currents")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Current (A)")
    ax.plot(t, df["i_in"], color="tab:blue", linewidth=1, label="Iin")
    ax.plot(t, df["i_out"], color="tab:red", linewidth=1, label="Iout")
    ax.legend(loc="upper left", fontsize=8)
    ax.grid(True, alpha=0.3)


def plot_temperatures(ax, df):
    """Panel: Thermocouple Temperatures."""
    t = df["elapsed_s"]
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


# ─────────────────────────────────────────────────────────────────────
# Combined and individual plot drivers
# ─────────────────────────────────────────────────────────────────────

PANELS = [
    ("power_wiper",  plot_power_wiper),
    ("voltages",     plot_voltages),
    ("currents",     plot_currents),
    ("temperatures", plot_temperatures),
]


def plot_combined(df, title="Zoë2 Power Board", save_path=None, dpi=150):
    """Generate combined 4-panel plot."""
    fig, axes = plt.subplots(2, 2, figsize=(14, 8))
    fig.suptitle(title, fontsize=14, fontweight="bold")

    plot_power_wiper(axes[0, 0], df)
    plot_voltages(axes[0, 1], df)
    plot_currents(axes[1, 0], df)
    plot_temperatures(axes[1, 1], df)

    fig.tight_layout(rect=[0, 0, 1, 0.95], h_pad=3, w_pad=3)

    t = df["elapsed_s"]
    duration = t.iloc[-1] - t.iloc[0] if len(t) > 1 else 0
    stats = (f"Samples: {len(df)}  |  Duration: {duration:.1f}s  |  "
             f"Avg Power: {df['power_in_W'].mean():.2f}W  |  "
             f"Avg Vin: {df['v_in'].mean():.2f}V")
    fig.text(0.5, 0.01, stats, ha="center", fontsize=9, color="gray")

    if save_path:
        fig.savefig(save_path, dpi=dpi, bbox_inches="tight")
        print(f"Saved combined plot to {save_path}")
    else:
        plt.show()


def save_individual(df, csv_path, title_prefix="Zoë2", dpi=150):
    """Save each panel as its own PNG file."""
    out_dir = csv_path.parent / (csv_path.stem + "_panels")
    out_dir.mkdir(exist_ok=True)

    for name, plot_fn in PANELS:
        fig, ax = plt.subplots(figsize=(10, 5))
        fig.suptitle(f"{title_prefix} — {name.replace('_', ' ').title()}",
                     fontsize=13, fontweight="bold")
        plot_fn(ax, df)
        fig.tight_layout(rect=[0, 0, 1, 0.95])
        out_path = out_dir / f"{name}.png"
        fig.savefig(out_path, dpi=dpi, bbox_inches="tight")
        plt.close(fig)
        print(f"  Saved {out_path}")

    print(f"Individual panels saved to {out_dir}/")


def main():
    ap = argparse.ArgumentParser(description="Plot Zoë2 datalogger CSV files")
    ap.add_argument("csvfile", type=str, help="Path to CSV log file")
    ap.add_argument("--save", action="store_true",
                    help="Save combined 4-panel PNG")
    ap.add_argument("--save-each", action="store_true",
                    help="Save each panel as a separate PNG")
    ap.add_argument("--dpi", type=int, default=150,
                    help="DPI for saved images (default: 150)")
    ap.add_argument("--title", type=str, default=None,
                    help="Custom plot title")
    ap.add_argument("--tstart", type=float, default=None,
                    help="Start time in elapsed_s (trim before this)")
    ap.add_argument("--tend", type=float, default=None,
                    help="End time in elapsed_s (trim after this)")
    args = ap.parse_args()

    csv_path = Path(args.csvfile)
    if not csv_path.exists():
        print(f"ERROR: File not found: {csv_path}")
        sys.exit(1)

    print(f"Loading {csv_path}...")
    df = load_csv(csv_path)
    print(f"  {len(df)} samples loaded")

    if args.tstart is not None:
        df = df[df["elapsed_s"] >= args.tstart]
    if args.tend is not None:
        df = df[df["elapsed_s"] <= args.tend]
    if args.tstart is not None or args.tend is not None:
        print(f"  {len(df)} samples after time filter "
              f"[{args.tstart or 'start'} – {args.tend or 'end'}]")
    if df.empty:
        print("ERROR: No data in the specified time range.")
        sys.exit(1)

    title = args.title or f"Zoë2 Power Board — {csv_path.stem}"

    if args.save_each:
        save_individual(df, csv_path, title_prefix=title, dpi=args.dpi)

    if args.save:
        save_path = csv_path.with_suffix(".png")
        plot_combined(df, title=title, save_path=save_path, dpi=args.dpi)
    elif not args.save_each:
        # neither --save nor --save-each: show interactively
        plot_combined(df, title=title, save_path=None, dpi=args.dpi)


if __name__ == "__main__":
    main()