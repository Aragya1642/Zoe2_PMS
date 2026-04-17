#!/usr/bin/env python3
"""
Zoë2 Power Board UART Data Logger & Plotter
=============================================
Connects to the STM32G4 power board over UART, parses the debug print
output (MPPT telemetry + thermocouple readings), logs everything to a
timestamped CSV file, and optionally displays live plots.

Expected UART output format (from main.c debug prints):
    MPPT: wiper=128  state=1  Pin=12.34W  Vin=45.67V  Iin=0.270A
          Vout=24.00V  Iout=0.500A
    ----
    TC3: 25.50 C  |  CJ3: 23.00 C
    TC2: 26.00 C  |  CJ2: 23.50 C
    TC1: 24.75 C  |  CJ1: 22.80 C
    Write Status: 0

Usage:
    python datalogger.py                        # auto-detect port
    python datalogger.py --port COM3            # specific port
    python datalogger.py --port /dev/ttyACM0    # Linux
    python datalogger.py --plot                 # enable live plotting
    python datalogger.py --plot --duration 300  # plot + stop after 5 min

Dependencies:
    pip install pyserial matplotlib
"""

import argparse
import csv
import os
import re
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)


# ─────────────────────────────────────────────────────────────────────
# Regex patterns for parsing UART lines
# ─────────────────────────────────────────────────────────────────────

MPPT_LINE1 = re.compile(
    r"MPPT:\s*wiper=(\d+)\s+state=(\d+)\s+Pin=([\d.-]+)W\s+"
    r"Vin=([\d.-]+)V\s+Iin=([\d.-]+)A"
)

MPPT_LINE2 = re.compile(
    r"Vout=([\d.-]+)V\s+Iout=([\d.-]+)A"
)

TC_LINE = re.compile(
    r"TC(\d):\s*([\d.-]+)\s*C\s*\|\s*CJ\d:\s*([\d.-]+)\s*C"
)

WIPER_STATUS = re.compile(
    r"Write Status:\s*(\d+)"
)


# ─────────────────────────────────────────────────────────────────────
# Data container
# ─────────────────────────────────────────────────────────────────────

class Sample:
    """One snapshot of all telemetry."""

    __slots__ = [
        "timestamp", "elapsed_s",
        # MPPT
        "wiper", "state", "power_in_W", "v_in", "i_in", "v_out", "i_out",
        "wiper_status",
        # Thermocouples
        "tc1", "cj1", "tc2", "cj2", "tc3", "cj3",
    ]

    CSV_HEADER = [
        "timestamp", "elapsed_s",
        "wiper", "state", "power_in_W", "v_in", "i_in", "v_out", "i_out",
        "wiper_status",
        "tc1", "cj1", "tc2", "cj2", "tc3", "cj3",
    ]

    def __init__(self):
        self.timestamp = ""
        self.elapsed_s = 0.0
        for field in self.__slots__[2:]:
            setattr(self, field, None)

    def as_row(self):
        return [getattr(self, f, "") for f in self.CSV_HEADER]

    def is_complete(self):
        """Return True if we have at least the MPPT data."""
        return self.wiper is not None and self.v_out is not None


# ─────────────────────────────────────────────────────────────────────
# Serial helpers
# ─────────────────────────────────────────────────────────────────────

def auto_detect_port():
    """Try to find an STM32 Virtual COM Port."""
    ports = serial.tools.list_ports.comports()
    for p in ports:
        desc = (p.description or "").lower()
        mfg = (p.manufacturer or "").lower()
        if any(kw in desc for kw in ["stm", "stlink", "serial", "usb"]):
            return p.device
        if any(kw in mfg for kw in ["stmicroelectronics", "stm"]):
            return p.device
    # Fallback: return first available
    if ports:
        return ports[0].device
    return None


def open_serial(port, baud=115200):
    """Open serial port with timeout."""
    ser = serial.Serial(port, baud, timeout=1)
    ser.reset_input_buffer()
    return ser


# ─────────────────────────────────────────────────────────────────────
# Parser
# ─────────────────────────────────────────────────────────────────────

class Parser:
    """Accumulates UART lines into complete Sample objects."""

    def __init__(self):
        self.current = Sample()
        self.start_time = time.time()

    def feed_line(self, line):
        """
        Feed one UART line. Returns a completed Sample when a separator
        '----' is seen, or None if still accumulating.
        """
        line = line.strip()
        if not line:
            return None

        # MPPT line 1
        m = MPPT_LINE1.search(line)
        if m:
            self.current.wiper      = int(m.group(1))
            self.current.state      = int(m.group(2))
            self.current.power_in_W = float(m.group(3))
            self.current.v_in       = float(m.group(4))
            self.current.i_in       = float(m.group(5))
            return None

        # MPPT line 2
        m = MPPT_LINE2.search(line)
        if m:
            self.current.v_out = float(m.group(1))
            self.current.i_out = float(m.group(2))
            return None

        # Thermocouple
        m = TC_LINE.search(line)
        if m:
            tc_num = int(m.group(1))
            tc_temp = float(m.group(2))
            cj_temp = float(m.group(3))
            if tc_num == 1:
                self.current.tc1, self.current.cj1 = tc_temp, cj_temp
            elif tc_num == 2:
                self.current.tc2, self.current.cj2 = tc_temp, cj_temp
            elif tc_num == 3:
                self.current.tc3, self.current.cj3 = tc_temp, cj_temp
            return None

        # Wiper write status
        m = WIPER_STATUS.search(line)
        if m:
            self.current.wiper_status = int(m.group(1))
            return None

        # Separator — emit completed sample
        if line.startswith("----"):
            if self.current.is_complete():
                self.current.timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                self.current.elapsed_s = round(time.time() - self.start_time, 3)
                completed = self.current
                self.current = Sample()
                return completed
            else:
                # Incomplete sample — discard and start fresh
                self.current = Sample()
                return None

        return None


# ─────────────────────────────────────────────────────────────────────
# Live plotter
# ─────────────────────────────────────────────────────────────────────

class LivePlotter:
    """Matplotlib-based live plotting with 4 subplots."""

    def __init__(self, max_points=500):
        import matplotlib.pyplot as plt
        self.plt = plt
        self.max_points = max_points

        self.plt.ion()
        self.fig, self.axes = self.plt.subplots(2, 2, figsize=(14, 8))
        self.fig.suptitle("Zoë2 Power Board — Live Telemetry", fontsize=14)
        self.fig.tight_layout(rect=[0, 0, 1, 0.95], h_pad=3, w_pad=3)

        # Data buffers
        self.t = []
        self.power = []
        self.v_in = []
        self.v_out = []
        self.i_in = []
        self.i_out = []
        self.wiper = []
        self.tc1 = []
        self.tc2 = []
        self.tc3 = []

        # Subplot 0: Power + Wiper
        ax = self.axes[0, 0]
        ax.set_title("Input Power & Wiper")
        ax.set_xlabel("Time (s)")
        ax.set_ylabel("Power (W)", color="tab:blue")
        self.line_power, = ax.plot([], [], "tab:blue", label="Pin (W)")
        self.ax_wiper = ax.twinx()
        self.ax_wiper.set_ylabel("Wiper", color="tab:orange")
        self.line_wiper, = self.ax_wiper.plot([], [], "tab:orange", label="Wiper")

        # Subplot 1: Voltages
        ax = self.axes[0, 1]
        ax.set_title("Voltages")
        ax.set_xlabel("Time (s)")
        ax.set_ylabel("Voltage (V)")
        self.line_vin, = ax.plot([], [], "tab:blue", label="Vin")
        self.line_vout, = ax.plot([], [], "tab:red", label="Vout")
        ax.legend(loc="upper left", fontsize=8)

        # Subplot 2: Currents
        ax = self.axes[1, 0]
        ax.set_title("Currents")
        ax.set_xlabel("Time (s)")
        ax.set_ylabel("Current (A)")
        self.line_iin, = ax.plot([], [], "tab:blue", label="Iin")
        self.line_iout, = ax.plot([], [], "tab:red", label="Iout")
        ax.legend(loc="upper left", fontsize=8)

        # Subplot 3: Temperatures
        ax = self.axes[1, 1]
        ax.set_title("Thermocouple Temperatures")
        ax.set_xlabel("Time (s)")
        ax.set_ylabel("Temperature (°C)")
        self.line_tc1, = ax.plot([], [], "tab:blue", label="TC1")
        self.line_tc2, = ax.plot([], [], "tab:orange", label="TC2")
        self.line_tc3, = ax.plot([], [], "tab:green", label="TC3")
        ax.legend(loc="upper left", fontsize=8)

        self.plt.show(block=False)
        self.plt.pause(0.01)

    def update(self, sample):
        """Push a new sample into the plot."""
        # Append data
        self.t.append(sample.elapsed_s)
        self.power.append(sample.power_in_W or 0)
        self.v_in.append(sample.v_in or 0)
        self.v_out.append(sample.v_out or 0)
        self.i_in.append(sample.i_in or 0)
        self.i_out.append(sample.i_out or 0)
        self.wiper.append(sample.wiper or 0)
        self.tc1.append(sample.tc1 if sample.tc1 is not None else float("nan"))
        self.tc2.append(sample.tc2 if sample.tc2 is not None else float("nan"))
        self.tc3.append(sample.tc3 if sample.tc3 is not None else float("nan"))

        # Trim to max_points
        if len(self.t) > self.max_points:
            trim = len(self.t) - self.max_points
            self.t = self.t[trim:]
            self.power = self.power[trim:]
            self.v_in = self.v_in[trim:]
            self.v_out = self.v_out[trim:]
            self.i_in = self.i_in[trim:]
            self.i_out = self.i_out[trim:]
            self.wiper = self.wiper[trim:]
            self.tc1 = self.tc1[trim:]
            self.tc2 = self.tc2[trim:]
            self.tc3 = self.tc3[trim:]

        # Update lines
        self.line_power.set_data(self.t, self.power)
        self.line_wiper.set_data(self.t, self.wiper)
        self.line_vin.set_data(self.t, self.v_in)
        self.line_vout.set_data(self.t, self.v_out)
        self.line_iin.set_data(self.t, self.i_in)
        self.line_iout.set_data(self.t, self.i_out)
        self.line_tc1.set_data(self.t, self.tc1)
        self.line_tc2.set_data(self.t, self.tc2)
        self.line_tc3.set_data(self.t, self.tc3)

        # Rescale axes
        for ax in self.axes.flat:
            ax.relim()
            ax.autoscale_view()
        self.ax_wiper.relim()
        self.ax_wiper.autoscale_view()

        self.fig.canvas.draw_idle()
        self.fig.canvas.flush_events()
        self.plt.pause(0.001)


# ─────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Zoë2 Power Board UART Logger & Plotter")
    ap.add_argument("--port", type=str, default=None,
                    help="Serial port (e.g. COM3, /dev/ttyACM0). Auto-detects if omitted.")
    ap.add_argument("--baud", type=int, default=115200,
                    help="Baud rate (default: 115200)")
    ap.add_argument("--plot", action="store_true",
                    help="Enable live matplotlib plotting")
    ap.add_argument("--duration", type=float, default=0,
                    help="Stop after N seconds (0 = run forever)")
    ap.add_argument("--outdir", type=str, default="logs",
                    help="Directory for CSV log files (default: logs/)")
    args = ap.parse_args()

    # Find serial port
    port = args.port or auto_detect_port()
    if port is None:
        print("ERROR: No serial port found. Specify with --port")
        sys.exit(1)

    # Create output directory
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    # CSV filename with timestamp
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path = outdir / f"zoe2_log_{ts}.csv"

    print(f"Port:     {port}")
    print(f"Baud:     {args.baud}")
    print(f"Log file: {csv_path}")
    print(f"Plotting: {'ON' if args.plot else 'OFF'}")
    if args.duration > 0:
        print(f"Duration: {args.duration} s")
    print()

    # Open serial
    try:
        ser = open_serial(port, args.baud)
    except serial.SerialException as e:
        print(f"ERROR: Could not open {port}: {e}")
        sys.exit(1)

    print(f"Connected to {port}. Press Ctrl+C to stop.\n")

    # Init parser, plotter, CSV
    parser = Parser()
    plotter = LivePlotter() if args.plot else None
    sample_count = 0
    start_time = time.time()

    try:
        with open(csv_path, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(Sample.CSV_HEADER)

            while True:
                # Check duration
                if args.duration > 0 and (time.time() - start_time) > args.duration:
                    print(f"\nDuration limit reached ({args.duration} s)")
                    break

                # Read line from UART
                try:
                    raw = ser.readline()
                except serial.SerialException:
                    print("Serial connection lost.")
                    break

                if not raw:
                    continue

                try:
                    line = raw.decode("utf-8", errors="replace").strip()
                except Exception:
                    continue

                if not line:
                    continue

                # Print raw UART to console
                print(f"  {line}")

                # Parse
                sample = parser.feed_line(line)
                if sample is not None:
                    sample_count += 1
                    writer.writerow(sample.as_row())
                    f.flush()

                    if plotter:
                        plotter.update(sample)

    except KeyboardInterrupt:
        print("\n\nStopped by user.")
    finally:
        ser.close()
        print(f"\nLogged {sample_count} samples to {csv_path}")

        if plotter:
            print("Close the plot window to exit.")
            plotter.plt.ioff()
            plotter.plt.show()


if __name__ == "__main__":
    main()