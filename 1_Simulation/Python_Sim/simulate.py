"""
Zoë2 MPPT Live Simulation — Animated Python Visualization
==========================================================
Real-time animated P&O + Periodic Full Scan MPPT algorithm visualization.
Includes proper multi-string partial shading model with bypass diodes
that produces multiple P-V curve peaks.

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
from dataclasses import dataclass, field
from typing import List, Tuple, Optional

# ============================================================
#  SOLAR PANEL MODEL
# ============================================================
#
#  The panel is modeled as N_strings groups of cells in series.
#  Each string has a bypass diode across it. When a string is
#  partially shaded, its photocurrent drops. If the string can't
#  support the panel current, its bypass diode activates, removing
#  that string's voltage contribution but allowing current to flow.
#
#  This produces the characteristic multi-peak P-V curve under
#  partial shading — which is exactly why we need periodic full
#  scans to find the global MPP.
# ============================================================

@dataclass
class PanelConfig:
    """Solar panel configuration.

    The panel has N_strings groups of cells in series, each with
    a bypass diode. Adjust to match your Zoë2 panel.
    """
    N_strings: int = 3            # Number of bypass-diode-protected strings
    cells_per_string: int = 20    # Cells per string
    I_sc: float = 8.0             # Short-circuit current (A) at STC per cell
    V_oc_cell: float = 0.68       # Open-circuit voltage per cell (V)
    n: float = 1.3                # Diode ideality factor
    R_s_cell: float = 0.008       # Series resistance per cell (Ω)
    V_bypass: float = -0.7        # Bypass diode forward voltage (V)
    T: float = 25.0               # Cell temperature (°C)

    @property
    def N_s(self):
        return self.N_strings * self.cells_per_string

    @property
    def V_oc_total(self):
        return self.N_s * self.V_oc_cell

    @property
    def V_t_cell(self):
        """Thermal voltage for a single cell."""
        return 0.02585 * (self.T + 273.15) / 298.15


def _string_iv(V_string, panel: PanelConfig, G: float):
    """Compute current for a single string of cells at irradiance G.

    Args:
        V_string: Voltage across the string (can be array)
        panel: Panel config
        G: Irradiance on this string (W/m²)

    Returns:
        Current through the string (A)
    """
    V_string = np.atleast_1d(np.float64(V_string))
    N = panel.cells_per_string
    V_t = panel.V_t_cell * N  # Thermal voltage for the string
    R_s = panel.R_s_cell * N

    I_ph = panel.I_sc * (G / 1000.0)
    V_oc_str = N * panel.V_oc_cell
    I_0 = I_ph / (np.exp(V_oc_str / (panel.n * V_t)) - 1)

    # Newton iteration to solve implicit I-V equation
    I = np.full_like(V_string, I_ph)
    for _ in range(20):
        exp_term = np.exp(np.clip((V_string + I * R_s) / (panel.n * V_t), -50, 50))
        f = I_ph - I - I_0 * (exp_term - 1)
        df = -1 - I_0 * R_s / (panel.n * V_t) * exp_term
        I = I - f / df

    return np.maximum(I, 0)


def panel_iv_curve(panel: PanelConfig, G_per_string: List[float], n_pts: int = 500):
    """Compute the full panel I-V curve under (possibly non-uniform) irradiance.

    Models bypass diodes: for a given panel current I, each string either
    conducts (contributing positive voltage) or is bypassed (contributing
    V_bypass ≈ -0.7V). We sweep current from 0 to max I_sc and compute
    the total panel voltage at each current.

    Args:
        panel: Panel configuration
        G_per_string: List of irradiance values, one per string (W/m²)
        n_pts: Number of points in the sweep

    Returns:
        (V_panel, I_panel, P_panel) arrays
    """
    assert len(G_per_string) == panel.N_strings

    # Max possible current is the highest string photocurrent
    I_max = max(panel.I_sc * (g / 1000.0) for g in G_per_string)
    I_sweep = np.linspace(0, I_max * 0.999, n_pts)

    # For each string, build a lookup: given current I, what voltage does it produce?
    V_total = np.zeros(n_pts)

    for s_idx, G in enumerate(G_per_string):
        N = panel.cells_per_string
        V_t = panel.V_t_cell * N
        R_s = panel.R_s_cell * N

        I_ph = panel.I_sc * (G / 1000.0)
        V_oc_str = N * panel.V_oc_cell
        I_0 = I_ph / (np.exp(V_oc_str / (panel.n * V_t)) - 1 + 1e-30)

        # For each sweep current, compute the string voltage
        # V = nVt * ln((Iph - I) / I0 + 1) - I*Rs  (explicit approx)
        I_clipped = np.clip(I_sweep, 0, I_ph - 1e-10)
        arg = (I_ph - I_clipped) / (I_0 + 1e-30) + 1
        arg = np.maximum(arg, 1e-10)
        V_string = panel.n * V_t * np.log(arg) - I_clipped * R_s

        # Bypass diode: if string can't support this current, diode activates
        # String is bypassed when I > I_ph (the string's max current)
        bypassed = I_sweep > I_ph
        V_string[bypassed] = panel.V_bypass

        V_total += V_string

    # Keep only positive voltage region
    valid = V_total > 0
    V_out = np.where(valid, V_total, 0)
    I_out = I_sweep
    P_out = V_out * I_out

    # Sort by voltage for clean plotting
    sort_idx = np.argsort(V_out)
    V_out = V_out[sort_idx]
    I_out = I_out[sort_idx]
    P_out = P_out[sort_idx]

    return V_out, I_out, P_out


def panel_power_at_voltage(V_target, panel: PanelConfig, G_per_string: List[float]):
    """Get panel current and power at a specific voltage by interpolation."""
    V_curve, I_curve, P_curve = panel_iv_curve(panel, G_per_string, n_pts=600)

    if V_target <= 0 or V_target >= V_curve[-1]:
        return 0.0, 0.0

    # Interpolate (V_curve is sorted)
    I_at_v = np.interp(V_target, V_curve, I_curve)
    P_at_v = V_target * I_at_v
    return I_at_v, P_at_v


def find_global_mpp(panel: PanelConfig, G_per_string: List[float]):
    """Find the global maximum power point."""
    V, I, P = panel_iv_curve(panel, G_per_string, n_pts=600)
    idx = np.argmax(P)
    return V[idx], P[idx], I[idx]


def find_local_peaks(P, min_prominence=5.0):
    """Find local maxima in the P-V curve (for visualization)."""
    peaks = []
    for i in range(1, len(P) - 1):
        if P[i] > P[i-1] and P[i] > P[i+1] and P[i] > min_prominence:
            peaks.append(i)
    return peaks


# ============================================================
#  MPPT ALGORITHM: P&O WITH PERIODIC FULL SCAN
# ============================================================

@dataclass
class MPPTConfig:
    delta_v: float = 0.5          # Perturbation step size (V)
    full_scan_interval: int = 50  # Iterations between full scans
    scan_points: int = 200        # Resolution of full voltage sweep
    v_min: float = 0.5            # Min operating voltage (V)
    v_max: float = 39.0           # Max operating voltage (V)


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


def mppt_step(state: MPPTState, config: MPPTConfig, panel: PanelConfig,
              G_per_string: List[float]):
    """Execute one MPPT iteration using the multi-string panel model."""
    state.iteration += 1

    if state.iteration % config.full_scan_interval == 1 or state.mode == "full_scan":
        # Full voltage sweep to find global MPP
        state.mode = "full_scan"
        V_curve, _, P_curve = panel_iv_curve(panel, G_per_string, config.scan_points)
        best_idx = np.argmax(P_curve)
        state.v_op = V_curve[best_idx]
        state.mode = "po"
    else:
        # Standard P&O
        state.mode = "po"
        _, p_cur = panel_power_at_voltage(state.v_op, panel, G_per_string)

        dp = p_cur - state.p_prev
        dv = state.v_op - state.v_prev

        state.v_prev = state.v_op
        state.p_prev = p_cur

        if dp > 0:
            state.v_op += config.delta_v if dv > 0 else -config.delta_v
        else:
            state.v_op += -config.delta_v if dv > 0 else config.delta_v

        state.v_op = np.clip(state.v_op, config.v_min, config.v_max)

    # Measure at operating point
    i_op, p_op = panel_power_at_voltage(state.v_op, panel, G_per_string)
    _, p_mpp, _ = find_global_mpp(panel, G_per_string)
    eff = (p_op / p_mpp * 100) if p_mpp > 0 else 0

    state.v_hist.append(state.v_op)
    state.p_hist.append(p_op)
    state.mpp_hist.append(p_mpp)
    state.eff_hist.append(eff)
    state.mode_hist.append("scan" if state.iteration % config.full_scan_interval == 1 else "po")
    state.irr_hist.append(G_per_string[0])  # Log first string for irradiance bar

    return p_op, i_op, eff, p_mpp


# ============================================================
#  IRRADIANCE SCENARIOS (now per-string)
# ============================================================

def scenario_steady(i, n_strings=3):
    """All strings at full sun."""
    return [1000.0] * n_strings

def scenario_cloud(i, n_strings=3):
    """Uniform cloud transient — all strings same irradiance."""
    if i < 80:    G = 1000.0
    elif i < 150: G = 400.0
    elif i < 220: G = 1000.0
    else:         G = 700.0
    return [G] * n_strings

def scenario_partial_shade(i, n_strings=3):
    """Partial shading — different strings get different irradiance.

    This creates multiple peaks on the P-V curve because shaded strings
    activate their bypass diodes at different current levels.
    """
    if i < 60:
        # Full sun
        return [1000.0] * n_strings
    elif i < 160:
        # One string heavily shaded, one partially, one full sun
        return [1000.0, 500.0, 200.0]
    elif i < 250:
        # Two strings shaded
        return [1000.0, 300.0, 300.0]
    elif i < 320:
        # Recovery — light partial shade
        return [1000.0, 800.0, 600.0]
    else:
        # Back to full sun
        return [1000.0] * n_strings

def scenario_moving_shadow(i, n_strings=3):
    """Shadow moving across the panel over time — strings shade sequentially."""
    base = 1000.0
    G_list = []
    for s in range(n_strings):
        # Each string has a shadow that peaks at a different time
        shadow_center = 100 + s * 80
        shadow_depth = 700.0
        shadow_width = 60.0
        shade = shadow_depth * np.exp(-0.5 * ((i - shadow_center) / shadow_width) ** 2)
        G_list.append(max(100.0, base - shade))
    return G_list

def scenario_gradual(i, n_strings=3):
    """Gradual uniform change — sunrise to sunset."""
    G = max(100.0, 600.0 + 400.0 * np.sin(2 * np.pi * i / 400))
    return [G] * n_strings

SCENARIOS = {
    "steady":         ("Steady State (1000 W/m²)", scenario_steady),
    "cloud":          ("Uniform Cloud Transient", scenario_cloud),
    "partial_shade":  ("Partial Shading (Multi-Peak)", scenario_partial_shade),
    "moving_shadow":  ("Moving Shadow", scenario_moving_shadow),
    "gradual":        ("Gradual Change (Sunrise → Sunset)", scenario_gradual),
}


# ============================================================
#  ANIMATED VISUALIZATION
# ============================================================

def run_simulation(scenario="partial_shade", total_steps=400, interval_ms=80):
    """Launch the animated MPPT simulation."""
    panel = PanelConfig()
    config = MPPTConfig(v_max=panel.V_oc_total - 1.0)
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
    PINK     = "#f472b6"

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

    fig = plt.figure(figsize=(15, 9.5))
    fig.suptitle(f"Zoë2 MPPT Simulation  —  P&O + Periodic Full Scan  —  {scenario_name}",
                 fontsize=13, fontweight="bold", color=TEXT_BR, y=0.97)

    # Layout: top-left is PV curve (larger), top-right is irradiance per string,
    # bottom-left is convergence, bottom-right is efficiency
    gs = fig.add_gridspec(2, 2, hspace=0.35, wspace=0.28,
                          top=0.92, bottom=0.06, left=0.06, right=0.97)
    ax_pv   = fig.add_subplot(gs[0, 0])
    ax_irr  = fig.add_subplot(gs[0, 1])
    ax_conv = fig.add_subplot(gs[1, 0])
    ax_eff  = fig.add_subplot(gs[1, 1])

    # Labels
    ax_pv.set_xlabel("Voltage (V)"); ax_pv.set_ylabel("Power (W)")
    ax_pv.set_title("P–V Curve & Operating Point", fontsize=10, color=TEXT, pad=8)

    ax_irr.set_xlabel("Iteration"); ax_irr.set_ylabel("Irradiance (W/m²)")
    ax_irr.set_title("Per-String Irradiance", fontsize=10, color=TEXT, pad=8)

    ax_conv.set_xlabel("Iteration"); ax_conv.set_ylabel("Power (W)")
    ax_conv.set_title("Power Convergence", fontsize=10, color=TEXT, pad=8)

    ax_eff.set_xlabel("Iteration"); ax_eff.set_ylabel("Efficiency (%)")
    ax_eff.set_title("Tracking Efficiency", fontsize=10, color=TEXT, pad=8)

    # ---- Reference P-V curve (full sun, fixed y-axis) ----
    G_full = [1000.0] * panel.N_strings
    V_ref, _, P_ref = panel_iv_curve(panel, G_full, 500)
    pv_y_max = np.max(P_ref) * 1.15
    pv_x_max = panel.V_oc_total * 1.05

    pv_ghost, = ax_pv.plot(V_ref, P_ref, color=BLUE, lw=1, alpha=0.12, ls='--', zorder=1)

    # PV objects
    pv_line, = ax_pv.plot([], [], color=BLUE, lw=2.5, alpha=0.9, zorder=2)
    pv_fill_ref = [None]
    pv_mpp_dot, = ax_pv.plot([], [], 'o', color=GREEN, ms=9, zorder=5)
    pv_op_dot, = ax_pv.plot([], [], 'o', color=RED, ms=11, zorder=6)
    pv_op_ring, = ax_pv.plot([], [], 'o', color=RED, ms=18, mfc='none', mew=1.5, alpha=0.5, zorder=5)
    pv_vline = ax_pv.axvline(0, color=RED, alpha=0.15, ls='--', lw=1)
    pv_hline = ax_pv.axhline(0, color=RED, alpha=0.15, ls='--', lw=1)
    pv_local_peaks, = ax_pv.plot([], [], 's', color=AMBER, ms=7, mfc='none', mew=1.5,
                                  zorder=4, label='Local peaks')
    pv_text = ax_pv.text(0.02, 0.96, "", transform=ax_pv.transAxes, fontsize=9,
                          va='top', color=TEXT_BR, fontweight='bold',
                          bbox=dict(boxstyle='round,pad=0.3', facecolor=BG_AX, alpha=0.8, edgecolor=GRID))

    ax_pv.set_xlim(0, pv_x_max)
    ax_pv.set_ylim(0, pv_y_max)

    # Irradiance per-string lines
    STRING_COLORS = [CYAN, AMBER, PINK, GREEN, PURPLE][:panel.N_strings]
    irr_lines = []
    irr_history = [[] for _ in range(panel.N_strings)]
    for s in range(panel.N_strings):
        line, = ax_irr.plot([], [], color=STRING_COLORS[s], lw=1.5,
                             label=f"String {s+1}", alpha=0.85)
        irr_lines.append(line)
    ax_irr.legend(fontsize=7, facecolor=BG_AX, edgecolor=GRID, labelcolor=TEXT, loc='lower left')
    ax_irr.set_ylim(0, 1200)

    # Convergence
    conv_mppt, = ax_conv.plot([], [], color=BLUE, lw=1.5, alpha=0.85)
    conv_mpp, = ax_conv.plot([], [], color=GREEN, lw=1.5, ls='--', alpha=0.6)
    conv_scans, = ax_conv.plot([], [], 'v', color=AMBER, ms=7, zorder=5)
    ax_conv.legend(["MPPT Power", "True MPP", "Full Scan"], loc='lower right',
                    fontsize=7, facecolor=BG_AX, edgecolor=GRID, labelcolor=TEXT)

    # Efficiency
    eff_line, = ax_eff.plot([], [], color=PURPLE, lw=1.5)
    eff_fill_ref = [None]
    ax_eff.axhline(90, color=RED, alpha=0.3, ls='--', lw=1)
    ax_eff.text(0.98, 0.17, "90% requirement", transform=ax_eff.transAxes,
                fontsize=7, ha='right', color=RED, alpha=0.5)
    eff_mean_line = ax_eff.axhline(0, color=PURPLE, alpha=0.4, ls=':', lw=1, visible=False)
    eff_mean_text = ax_eff.text(0.02, 0.04, "", transform=ax_eff.transAxes, fontsize=8,
                                 color=PURPLE, fontweight='bold')
    ax_eff.set_ylim(0, 105)

    iter_text = fig.text(0.5, 0.01, "", ha='center', fontsize=10, color=TEXT, fontweight='bold')

    # ---- Animation ----
    def update(frame):
        G_list = irr_func(frame, panel.N_strings)
        p_op, i_op, eff, p_mpp = mppt_step(state, config, panel, G_list)
        v_op = state.v_op
        iters = np.arange(len(state.p_hist))

        # ---- P-V CURVE ----
        V_curve, I_curve, P_curve = panel_iv_curve(panel, G_list, 500)
        pv_line.set_data(V_curve, P_curve)

        if pv_fill_ref[0] is not None:
            pv_fill_ref[0].remove()
        pv_fill_ref[0] = ax_pv.fill_between(V_curve, P_curve, alpha=0.06, color=BLUE, zorder=1)

        # Global MPP marker
        v_mpp_val, p_mpp_val, _ = find_global_mpp(panel, G_list)
        pv_mpp_dot.set_data([v_mpp_val], [p_mpp_val])

        # Local peaks (for multi-peak visualization)
        peak_idx = find_local_peaks(P_curve, min_prominence=3.0)
        if peak_idx:
            pv_local_peaks.set_data([V_curve[i] for i in peak_idx],
                                     [P_curve[i] for i in peak_idx])
        else:
            pv_local_peaks.set_data([], [])

        # Operating point
        pv_op_dot.set_data([v_op], [p_op])
        pv_op_ring.set_data([v_op], [p_op])
        pv_vline.set_xdata([v_op])
        pv_hline.set_ydata([p_op])

        n_peaks = len(peak_idx)
        peak_str = f"  [{n_peaks} peak{'s' if n_peaks != 1 else ''}]" if n_peaks > 1 else ""
        pv_text.set_text(f"MPPT: {p_op:.1f}W @ {v_op:.1f}V\n"
                         f"MPP:  {p_mpp:.1f}W @ {v_mpp_val:.1f}V{peak_str}")

        # ---- IRRADIANCE PER STRING ----
        for s in range(panel.N_strings):
            irr_history[s].append(G_list[s])
            irr_lines[s].set_data(np.arange(len(irr_history[s])), irr_history[s])
        ax_irr.set_xlim(0, max(len(irr_history[0]), 50))

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

        # ---- EFFICIENCY ----
        eff_line.set_data(iters, state.eff_hist)

        if eff_fill_ref[0] is not None:
            eff_fill_ref[0].remove()
        if len(state.eff_hist) > 1:
            eff_fill_ref[0] = ax_eff.fill_between(iters, state.eff_hist, alpha=0.08, color=PURPLE)
        else:
            eff_fill_ref[0] = None

        ax_eff.set_xlim(0, max(len(state.eff_hist), 50))

        if len(state.eff_hist) > 15:
            mean_eff = np.mean(state.eff_hist[10:])
            eff_mean_line.set_ydata([mean_eff])
            eff_mean_line.set_visible(True)
            eff_mean_text.set_text(f"Mean: {mean_eff:.1f}%")

        iter_text.set_text(
            f"Iteration {state.iteration}  |  "
            f"G = [{', '.join(f'{g:.0f}' for g in G_list)}] W/m²  |  "
            f"{scenario_name}"
        )

        return []

    anim = animation.FuncAnimation(
        fig, update,
        frames=total_steps,
        interval=interval_ms,
        blit=False,
        repeat=False,
    )

    plt.show()
    return anim


# ============================================================
#  MAIN
# ============================================================

if __name__ == "__main__":
    # =============================================
    #  CONFIGURATION — CHANGE THESE AS NEEDED
    # =============================================

    SCENARIO = "partial_shade"  # "steady", "cloud", "partial_shade", "moving_shadow", "gradual"
    TOTAL_STEPS = 400
    SPEED_MS = 80               # ms per frame (lower = faster)

    print("=" * 60)
    print("  Zoë2 MPPT Live Simulation")
    print("  P&O with Periodic Full Scans")
    print(f"  Scenario: {SCENARIOS[SCENARIO][0]}")
    print(f"  Speed: {SPEED_MS}ms/frame  |  Steps: {TOTAL_STEPS}")
    print("=" * 60)
    print("  Close the plot window to stop.\n")

    anim = run_simulation(
        scenario=SCENARIO,
        total_steps=TOTAL_STEPS,
        interval_ms=SPEED_MS,
    )