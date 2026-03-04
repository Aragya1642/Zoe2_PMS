"""
Zoë2 MPPT Live Simulation — Animated Python Visualization
==========================================================

Author: Aragya Goyal
"""

# Package Imports

# Globals
SCENARIOS = {
    "steady":           ("Steady State (1000 W/m²)", scenario_steady),
    "uniform shading":  ("Uniform Cloud Transient", scenario_cloud),
    "partial shading":  ("Partial Shading (Multi-Peak)", scenario_partial_shade)
}

# Functions
def scenario_steady(i, n_strings=3):
    return [1000] * n_strings

VISUAL_UPDATE_MS = 50  # Milliseconds per frame (lower = faster)


if __name__ == "__main__":
    print("=" * 55)
    print("  Zoë2 MPPT Live Simulation")
    print("=" * 55)
    print("  Close the plot window to stop.\n")