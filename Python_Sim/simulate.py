"""
Zoë2 MPPT Live Simulation — Animated Python Visualization
==========================================================
Real-time animated P&O + Periodic Full Scan MPPT algorithm visualization.
Uses matplotlib FuncAnimation for smooth updates.

Controls:
  - Close the window to stop
  - Modify PANEL_PARAMS and MPPT_CONFIG below to match your hardware
  - Change SCENARIO at the bottom to switch irradiance profiles

Author: Aragya
Project: Zoë2 Rover — Carnegie Mellon Planetary Robotics Laboratory
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.patches import FancyArrowPatch
from dataclasses import dataclass, field
from typing import List, Tuple

# ============================================================
#  SOLAR PANEL MODEL (Single-Diode)
# ============================================================

@dataclass
class SolarPanelParams:
    """Adjust these to match your actual Zoë2 panel specs."""
    V_oc: float = 40.0       # Open-circuit voltage (V)
    I_sc: float = 8.0        # Short-circuit current (A)
    V_mpp: float = 32.0      # Voltage at MPP (V)
    I_mpp: float = 7.5       # Current at MPP (A)
    N_s: int = 60            # Cells in series
    K_v: float = -0.0032     # Voltage temp coeff (%/°C)
    K_i: float = 0.0005      # Current temp coeff (%/°C)
    n: float = 1.3           # Diode ideality factor
    R_s: float = 0.5         # Series resistance (Ω)
    R_sh: float = 200.0      # Shunt resistance (Ω)

    def __post_init__(self):
        self.V_t = 0.02585 * self.N_s


def solar_current(V, panel, G=1000.0, T=25.0):
    """Single-diode model current. Works with scalars or arrays."""
    V = np.atleast_1d(np.float64(V))
    dT = T - 25.0
    I_ph = (panel.I_sc + panel.K_i * dT) * (G / 1000.0)
    V_t = panel.V_t * (T + 273.15) / 298.15
    V_oc_adj = panel.V_oc + panel.K_v * dT
    I_0 = I_ph / (np.exp(V_oc_adj / (panel.n * V_t)) - 1)

    I = np.full_like(V, I_ph)
    for _ in range(15):
        exp_term = np.exp((V + I * panel.R_s) / (panel.n * V_t))
        f = I_ph - I - I_0 * (exp_term - 1) - (V + I * panel.R_s) / panel.R_sh
        df = -1 - I_0 * panel.R_s / (panel.n * V_t) * exp_term - panel.R_s / panel.R_sh
        I = I - f / df
    return np.maximum(I, 0)


def get_pv_curve(panel, G=1000.0, T=25.0, n_pts=300):
    V = np.linspace(0, panel.V_oc * 1.05, n_pts)
    I = solar_current(V, panel, G, T)
    P = V * I
    return V, I, P


def true_mpp(panel, G, T=25.0):
    V, _, P = get_pv_curve(panel, G, T, 300)
    idx = np.argmax(P)
    return V[idx], P[idx]


# ============================================================
#  MPPT ALGORITHM
# ============================================================

@dataclass
class MPPTConfig:
    delta_v: float = 0.5
    full_scan_interval: int = 50
    scan_points: int = 100
    v_min: float = 0.5
    v_max: float = 39.0


@dataclass
class MPPTState:
    v_op: float = 5.0
    p_prev: float = 0.0
    v_prev: float = 5.0
    iteration: int = 0
    mode: str = "full_scan"

    # History
    v_hist: List[float] = field(default_factory=list)
    p_hist: List[float] = field(default_factory=list)
    mpp_hist: List[float] = field(default_factory=list)
    eff_hist: List[float] = field(default_factory=list)
    mode_hist: List[str] = field(default_factory=list)
    irr_hist: List[float] = field(default_factory=list)


def mppt_step(state, config, panel, G, T=25.0):
    state.iteration += 1

    if state.iteration % config.full_scan_interval == 1 or state.mode == "full_scan":
        state.mode = "full_scan"
        v_sweep = np.linspace(config.v_min, config.v_max, config.scan_points)
        i_sweep = solar_current(v_sweep, panel, G, T)
        p_sweep = v_sweep * i_sweep
        state.v_op = v_sweep[np.argmax(p_sweep)]
        state.mode = "po"
    else:
        state.mode = "po"
        i_cur = solar_current(state.v_op, panel, G, T)[0]
        p_cur = state.v_op * i_cur
        dp = p_cur - state.p_prev
        dv = state.v_op - state.v_prev

        state.v_prev = state.v_op
        state.p_prev = p_cur

        if dp > 0:
            state.v_op += config.delta_v if dv > 0 else -config.delta_v
        else:
            state.v_op += -config.delta_v if dv > 0 else config.delta_v

        state.v_op = np.clip(state.v_op, config.v_min, config.v_max)

    i_op = solar_current(state.v_op, panel, G, T)[0]
    p_op = state.v_op * i_op
    v_mpp, p_mpp = true_mpp(panel, G, T)
    eff = (p_op / p_mpp * 100) if p_mpp > 0 else 0

    state.v_hist.append(state.v_op)
    state.p_hist.append(p_op)
    state.mpp_hist.append(p_mpp)
    state.eff_hist.append(eff)
    state.mode_hist.append("scan" if state.iteration % config.full_scan_interval == 1 else "po")
    state.irr_hist.append(G)

    return p_op, i_op, eff, p_mpp


# ============================================================
#  IRRADIANCE SCENARIOS
# ============================================================

def irradiance_steady(i):
    return 1000.0

def irradiance_cloud(i):
    if i < 80: return 1000.0
    elif i < 150: return 400.0
    elif i < 220: return 1000.0
    else: return 700.0

def irradiance_gradual(i):
    return max(100.0, 600.0 + 400.0 * np.sin(2 * np.pi * i / 400))

def irradiance_partial_shade(i):
    base = 700 + 200 * np.sin(2 * np.pi * i / 100)
    flicker = -250 if (i % 30 < 10) else 0
    return max(100.0, base + flicker)

SCENARIOS = {
    "steady":        ("Steady State (1000 W/m²)", irradiance_steady),
    "cloud":         ("Cloud Transient", irradiance_cloud),
    "gradual":       ("Gradual Change (Sunrise → Sunset)", irradiance_gradual),
    "partial_shade": ("Partial Shading", irradiance_partial_shade),
}


# ============================================================
#  ANIMATED VISUALIZATION
# ============================================================

def run_simulation(scenario="cloud", total_steps=400, interval_ms=80):
    """
    Launch the animated MPPT simulation.

    Args:
        scenario: One of "steady", "cloud", "gradual", "partial_shade"
        total_steps: Total simulation iterations
        interval_ms: Milliseconds between frames (lower = faster)
    """
    panel = SolarPanelParams()
    config = MPPTConfig(v_max=panel.V_oc - 1.0)
    state = MPPTState()
    irr_func = SCENARIOS[scenario][1]
    scenario_name = SCENARIOS[scenario][0]

    # ---- Style ----
    BG       = "#0a0e17"
    BG_AX    = "#0f1520"
    GRID     = "#1a2236"
    TEXT     = "#8892a8"
    TEXT_BR  = "#cbd5e1"
    BLUE     = "#60a5fa"
    GREEN    = "#34d399"
    RED      = "#f87171"
    AMBER    = "#f59e0b"
    PURPLE   = "#a78bfa"
    CYAN     = "#06b6d4"

    plt.rcParams.update({
        "figure.facecolor": BG,
        "axes.facecolor": BG_AX,
        "axes.edgecolor": GRID,
        "axes.labelcolor": TEXT,
        "axes.grid": True,
        "grid.color": GRID,
        "grid.linewidth": 0.5,
        "xtick.color": TEXT,
        "ytick.color": TEXT,
        "text.color": TEXT_BR,
        "font.family": "monospace",
        "font.size": 9,
    })

    fig, axes = plt.subplots(2, 2, figsize=(14, 8.5))
    fig.suptitle(f"Zoë2 MPPT Simulation  —  P&O + Periodic Full Scan  —  {scenario_name}",
                 fontsize=13, fontweight="bold", color=TEXT_BR, y=0.97)
    fig.subplots_adjust(hspace=0.35, wspace=0.28, top=0.92, bottom=0.08, left=0.07, right=0.97)

    ax_pv, ax_conv, ax_volt, ax_eff = axes[0, 0], axes[0, 1], axes[1, 0], axes[1, 1]

    # ---- Static labels ----
    ax_pv.set_xlabel("Voltage (V)")
    ax_pv.set_ylabel("Power (W)")
    ax_pv.set_title("P–V Curve & Operating Point", fontsize=10, color=TEXT, pad=8)

    ax_conv.set_xlabel("Iteration")
    ax_conv.set_ylabel("Power (W)")
    ax_conv.set_title("Power Convergence", fontsize=10, color=TEXT, pad=8)

    ax_volt.set_xlabel("Iteration")
    ax_volt.set_ylabel("Voltage (V)")
    ax_volt.set_title("Voltage Trajectory", fontsize=10, color=TEXT, pad=8)

    ax_eff.set_xlabel("Iteration")
    ax_eff.set_ylabel("Efficiency (%)")
    ax_eff.set_title("Tracking Efficiency", fontsize=10, color=TEXT, pad=8)

    # ---- Persistent plot objects (updated each frame) ----
    # PV curve
    pv_line, = ax_pv.plot([], [], color=BLUE, lw=2, alpha=0.9)
    pv_fill = None
    pv_mpp_dot, = ax_pv.plot([], [], 'o', color=GREEN, ms=8, zorder=5)
    pv_op_dot, = ax_pv.plot([], [], 'o', color=RED, ms=10, zorder=6)
    pv_op_ring, = ax_pv.plot([], [], 'o', color=RED, ms=16, mfc='none', mew=1.5, alpha=0.5, zorder=5)
    pv_vline = ax_pv.axvline(0, color=RED, alpha=0.15, ls='--', lw=1)
    pv_hline = ax_pv.axhline(0, color=RED, alpha=0.15, ls='--', lw=1)
    pv_text = ax_pv.text(0.02, 0.96, "", transform=ax_pv.transAxes, fontsize=9,
                          va='top', color=TEXT_BR, fontweight='bold')
    pv_irr_text = ax_pv.text(0.98, 0.96, "", transform=ax_pv.transAxes, fontsize=9,
                              va='top', ha='right', color=AMBER)

    # Convergence
    conv_mppt, = ax_conv.plot([], [], color=BLUE, lw=1.5, alpha=0.85)
    conv_mpp, = ax_conv.plot([], [], color=GREEN, lw=1.5, ls='--', alpha=0.6)
    conv_scans, = ax_conv.plot([], [], 'v', color=AMBER, ms=7, zorder=5)
    ax_conv.legend(["MPPT Power", "True MPP", "Full Scan"], loc='lower right',
                    fontsize=7, facecolor=BG_AX, edgecolor=GRID, labelcolor=TEXT)

    # Voltage
    volt_line, = ax_volt.plot([], [], color=AMBER, lw=1.5)
    volt_scans, = ax_volt.plot([], [], 'o', color=AMBER, ms=5, zorder=5)

    # Efficiency
    eff_line, = ax_eff.plot([], [], color=PURPLE, lw=1.5)
    eff_fill = None
    eff_req = ax_eff.axhline(90, color=RED, alpha=0.3, ls='--', lw=1)
    ax_eff.text(0.98, 0.17, "90% requirement", transform=ax_eff.transAxes,
                fontsize=7, ha='right', color=RED, alpha=0.5)
    eff_mean_line = ax_eff.axhline(0, color=PURPLE, alpha=0.4, ls=':', lw=1, visible=False)
    eff_mean_text = ax_eff.text(0.02, 0.04, "", transform=ax_eff.transAxes, fontsize=8,
                                 color=PURPLE, fontweight='bold')

    # Iteration counter (bottom center of figure)
    iter_text = fig.text(0.5, 0.01, "", ha='center', fontsize=10, color=TEXT, fontweight='bold')

    # ---- Animation function ----
    def update(frame):
        nonlocal pv_fill, eff_fill

        G = irr_func(frame)
        p_op, i_op, eff, p_mpp = mppt_step(state, config, panel, G)
        v_op = state.v_op
        iters = np.arange(len(state.p_hist))

        # ---- P-V CURVE ----
        V_curve, _, P_curve = get_pv_curve(panel, G, 25.0, 200)
        pv_line.set_data(V_curve, P_curve)

        # Fill under curve
        if pv_fill is not None:
            pv_fill.remove()
        pv_fill = ax_pv.fill_between(V_curve, P_curve, alpha=0.06, color=BLUE)

        max_p = max(np.max(P_curve), 1)
        ax_pv.set_xlim(0, panel.V_oc * 1.05)
        ax_pv.set_ylim(0, max_p * 1.15)

        # True MPP
        v_mpp_val, p_mpp_val = true_mpp(panel, G)
        pv_mpp_dot.set_data([v_mpp_val], [p_mpp_val])

        # Operating point
        pv_op_dot.set_data([v_op], [p_op])
        pv_op_ring.set_data([v_op], [p_op])
        pv_vline.set_xdata([v_op])
        pv_hline.set_ydata([p_op])

        pv_text.set_text(f"MPPT: {p_op:.1f}W @ {v_op:.1f}V\nMPP:  {p_mpp:.1f}W @ {v_mpp_val:.1f}V")
        pv_irr_text.set_text(f"G = {G:.0f} W/m²")

        # ---- CONVERGENCE ----
        conv_mppt.set_data(iters, state.p_hist)
        conv_mpp.set_data(iters, state.mpp_hist)

        scan_idx = [i for i, m in enumerate(state.mode_hist) if m == "scan"]
        scan_p = [state.p_hist[i] for i in scan_idx]
        conv_scans.set_data(scan_idx, scan_p)

        ax_conv.set_xlim(0, max(len(state.p_hist), 50))
        all_p = state.p_hist + state.mpp_hist
        if all_p:
            ax_conv.set_ylim(0, max(all_p) * 1.15)

        # ---- VOLTAGE ----
        volt_line.set_data(iters, state.v_hist)
        scan_v = [state.v_hist[i] for i in scan_idx]
        volt_scans.set_data(scan_idx, scan_v)

        ax_volt.set_xlim(0, max(len(state.v_hist), 50))
        if state.v_hist:
            vmin = max(0, min(state.v_hist) - 3)
            vmax = max(state.v_hist) + 3
            ax_volt.set_ylim(vmin, vmax)

        # ---- EFFICIENCY ----
        eff_line.set_data(iters, state.eff_hist)

        if eff_fill is not None:
            eff_fill.remove()
        if len(state.eff_hist) > 1:
            eff_fill = ax_eff.fill_between(iters, state.eff_hist, alpha=0.08, color=PURPLE)
        else:
            eff_fill = None

        ax_eff.set_xlim(0, max(len(state.eff_hist), 50))
        ax_eff.set_ylim(0, 105)

        # Mean efficiency (skip first 10 to ignore cold start)
        if len(state.eff_hist) > 15:
            mean_eff = np.mean(state.eff_hist[10:])
            eff_mean_line.set_ydata([mean_eff])
            eff_mean_line.set_visible(True)
            eff_mean_text.set_text(f"Mean: {mean_eff:.1f}%")

        # Iteration counter
        iter_text.set_text(f"Iteration {state.iteration}  |  Scenario: {scenario_name}")

        return (pv_line, pv_mpp_dot, pv_op_dot, pv_op_ring, pv_vline, pv_hline,
                conv_mppt, conv_mpp, conv_scans,
                volt_line, volt_scans,
                eff_line, eff_mean_line,
                pv_text, pv_irr_text, eff_mean_text, iter_text)

    anim = animation.FuncAnimation(
        fig, update,
        frames=total_steps,
        interval=interval_ms,
        blit=False,
        repeat=False,
    )

    plt.show()
    return anim  # Keep reference to prevent garbage collection


# ============================================================
#  MAIN
# ============================================================

if __name__ == "__main__":
    # =============================================
    #  CONFIGURATION — CHANGE THESE AS NEEDED
    # =============================================

    SCENARIO = "cloud"          # "steady", "cloud", "gradual", "partial_shade"
    TOTAL_STEPS = 400           # Total iterations to simulate
    SPEED_MS = 80               # Milliseconds per frame (lower = faster)

    print("=" * 55)
    print("  Zoë2 MPPT Live Simulation")
    print("  P&O with Periodic Full Scans")
    print(f"  Scenario: {SCENARIOS[SCENARIO][0]}")
    print(f"  Speed: {SPEED_MS}ms/frame  |  Steps: {TOTAL_STEPS}")
    print("=" * 55)
    print("  Close the plot window to stop.\n")

    anim = run_simulation(
        scenario=SCENARIO,
        total_steps=TOTAL_STEPS,
        interval_ms=SPEED_MS,
    )