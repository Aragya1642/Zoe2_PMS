#!/usr/bin/env python3
"""
Simple CAN Log Parser
=====================
Parses candump log files into structured fields ready for post-processing.
Add your own decoding logic in the process_message() function.

Usage: python can_parser.py <logfile> [outputfile]
"""

import re
import sys
from datetime import datetime, timezone


# candump format: (timestamp) interface canid#data
LOG_PATTERN = re.compile(r"\((\d+\.\d+)\)\s+(\S+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]+)")


def parse_can_id(can_id_hex):
    """Break extended 29-bit CAN ID into protocol fields."""
    can_id = int(can_id_hex, 16)
    return {
        "priority":    (can_id >> 24) & 0xFF,
        "func_code":   (can_id >> 16) & 0xFF,
        "source_addr": can_id & 0xFFFF,
        "module_num":  can_id & 0x00FF,
    }


def parse_line(line):
    """Parse one candump log line into a dict, or None if unparseable."""
    m = LOG_PATTERN.match(line.strip())
    if not m:
        return None

    timestamp = float(m.group(1))
    data_hex = m.group(4)
    data_bytes = bytes.fromhex(data_hex)
    can_info = parse_can_id(m.group(3))

    return {
        "timestamp":   timestamp,
        "time_utc":    datetime.fromtimestamp(timestamp, tz=timezone.utc),
        "interface":   m.group(2),
        "can_id_hex":  m.group(3).upper(),
        "can_id_int":  int(m.group(3), 16),
        "func_code":   can_info["func_code"],
        "source_addr": can_info["source_addr"],
        "module_num":  can_info["module_num"],
        "data_hex":    data_hex.upper(),
        "data_bytes":  data_bytes,
        "data_len":    len(data_bytes),
    }


# ─────────────────────────────────────────────────────────────────────
# POST-PROCESSING: Add your own decoding logic here
# ─────────────────────────────────────────────────────────────────────

def process_message(msg, out):
    """
    Called for every parsed CAN message. Writes decoded output to `out` file handle.

    msg fields available:
      msg["timestamp"]    - float, unix epoch
      msg["time_utc"]     - datetime object (UTC)
      msg["interface"]    - e.g. "can0"
      msg["can_id_hex"]   - e.g. "04008001"
      msg["can_id_int"]   - integer CAN ID
      msg["func_code"]    - int, function code extracted from CAN ID
      msg["source_addr"]  - int, source address (full 16-bit)
      msg["module_num"]   - int, low byte of source (module number)
      msg["data_hex"]     - e.g. "010E240E240E23FF"
      msg["data_bytes"]   - bytes object
      msg["data_len"]     - int, number of data bytes

    Useful helpers for reading data_bytes:
      int.from_bytes(msg["data_bytes"][0:2], "big")   # big-endian u16
      msg["data_bytes"][3]                             # single byte
    """

    fc = msg["func_code"]
    d = msg["data_bytes"]
    t = msg["time_utc"].strftime("%H:%M:%S.%f")[:-3]

    # One-line summary per message
    out.write(f"[{t}] ID={msg['can_id_hex']}  func=0x{fc:02X}  mod={msg['module_num']}  data={msg['data_hex']}\n")

    # Decode Data
    if not msg["can_id_hex"] == "0400FF80":
        
        if fc == 0x00:      # Cell Voltages
            group = d[0]
            base = (group - 1) * 3 + 1
            for i, offset in enumerate([(1,3), (3,5), (5,7)]):
                cell_num = base + i
                if cell_num > 20:  # skip non-cell channel (21st cell)
                    continue
                mv = int.from_bytes(d[offset[0]:offset[1]], "big")
                out.write(f"  Cell {cell_num}: {mv} mV\n")
        elif fc == 0x01:    # Temperatures
            group = d[0]
            t1 = int.from_bytes(d[1:2], "big") - 40 # Offset of 40°C
            t2 = int.from_bytes(d[2:3], "big") - 40
            out.write(f"    Temperature group {group}: {t1} {t2} °C\n")
        elif fc == 0x02:    # Total Information 0
            sum_v = int.from_bytes(d[0:2], "big") * 0.1 # Scale factor of 0.1 V/bit
            curr = int.from_bytes(d[2:4], "big") - 30000 * 0.1 # Scale factor of 0.1 A/bit with offset of -3000 A
            soc = int.from_bytes(d[4:6], "big") * 0.1 # Scale factor of 0.1 %
            life = d[6] # Life cycle count
            out.write(f"    Total voltage: {sum_v}mV  Current: {curr}mA  SOC: {soc}%  Life: {life}\n")
        elif fc == 0x03:    # Total Information 1
            power = int.from_bytes(d[0:2], "big")
            # tot_energy = int.from_bytes(d[2:4], "big")
            mos_temp = d[4] - 40 # Offset of 40°C
            # board_temp = d[5] - 40 # Offset of 40°C
            # heat_temp = d[6] - 40 # Offset of 40°C
            # heat_curr = d[7]
            out.write(f"    Power: {power}W  MOS Temp: {mos_temp}°C\n")
        elif fc == 0x04:    # Cell Voltage Statistical Info
            max_v = int.from_bytes(d[0:2], "big")
            max_v_num = d[2]
            min_v = int.from_bytes(d[3:5], "big")
            min_v_num = d[5]
            diff_v = int.from_bytes(d[6:8], "big")
            out.write(f"    Max Voltage: {max_v}mV  Max Voltage Cell: {max_v_num}  Min Voltage: {min_v}mV  Min Voltage Cell: {min_v_num}  Diff Voltage: {diff_v}mV\n")
        elif fc == 0x05:    # Unit Temperature Statistical Information
            max_temp = d[0] - 40 # Offset of 40°C
            max_temp_num = d[1]
            min_temp = d[2] - 40 # Offset of 40°C
            min_temp_num = d[3]
            diff_temp = d[4]
            out.write(f"    Max Temp: {max_temp}°C  Max Temp Unit: {max_temp_num}  Min Temp: {min_temp}°C  Min Temp Unit: {min_temp_num}  Diff Temp: {diff_temp}°C\n")
        elif fc == 0x06:    # Status Information 0
            chg_mos_state = d[0]
            dischg_mos_state = d[1]
            pre_mos_state = d[2]
            heat_mos_state = d[3]
            fan_mos_state = d[4]
            out.write(f"    MOS States - Charge: {chg_mos_state}  Discharge: {dischg_mos_state}  Precharge: {pre_mos_state}  Heat: {heat_mos_state}  Fan: {fan_mos_state}\n")
        elif fc == 0x07:    # Status Information 1 - TODO: verify bit meanings
            bat_state = d[0]
            chg_detect = d[1]
            load_detect = d[2]
            DO_state = d[3]
            DI_state = d[4]
            out.write(f"    Battery State: {bat_state}  Charge Detect: {chg_detect}  Load Detect: {load_detect}  DO State: {DO_state}  DI State: {DI_state} (Might be wrong)\n")
        elif fc == 0x08:    # Status Information 2
            cell_number = d[0]
            ntc_number = d[1]
            remain_capacity = int.from_bytes(d[2:6], "big")
            cycle_time = int.from_bytes(d[6:8], "big")
            out.write(f"    Cell Number: {cell_number}  NTC Number: {ntc_number}  Remain Capacity: {remain_capacity}mAh  Cycle Time: {cycle_time}\n")
        elif fc == 0x09:    # Hardware/Battery Failure
            pass
        elif fc == 0x0A:    # Equilibrium State Info
            balance_state = d[0]
            balance_cur = int.from_bytes(d[2:4], "big")
            balance_1_8_state = d[4]
            balance_9_16_state = d[5]
            balance_17_24_state = d[6]
            balance_25_32_state = d[7]
            out.write(f"    Balance State: {balance_state}  Balance Current: {balance_cur}mA  Balance 1-8 State: {balance_1_8_state}  Balance 9-16 State: {balance_9_16_state}  Balance 17-24 State: {balance_17_24_state}  Balance 25-32 State: {balance_25_32_state}\n")
        elif fc == 0x0B:    # Charging Information
            rest_chg_time = int.from_bytes(d[0:2], "big")
            wakeup_source = d[2]
            out.write(f"    Remaining Charge Time: {rest_chg_time} minutes  Wakeup Source: {wakeup_source}\n")
        elif fc == 0x0C:    # Calendar
            year = 2000 + d[0] # Year offset from 2000
            month = d[1]
            day = d[2]
            hour = d[3]
            minute = d[4]
            second = d[5]
            out.write(f"    Calendar: {year:04d}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:02d}\n")
        elif fc == 0x0D:    # Limiting
            limit_cur_state = d[0]
            limit_cur = int.from_bytes(d[1:3], "big")
            state_of_health = int.from_bytes(d[3:5], "big") 
            pwm_duty = int.from_bytes(d[5:7], "big")
            out.write(f"    Limit Current State: {limit_cur_state}  Limit Current: {limit_cur}mA  State of Health: {state_of_health}%  PWM Duty: {pwm_duty}%\n")
        elif fc == 0x0E:    # Faults
            pass
        elif fc == 0x0F:    # AFE Data
            pass

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <candump_logfile> [output_file]")
        sys.exit(1)

    logfile = sys.argv[1]

    # Default output filename: input stem + "_parsed.txt"
    if len(sys.argv) >= 3:
        outfile = sys.argv[2]
    else:
        from pathlib import Path
        outfile = str(Path(logfile).stem) + "_parsed.txt"

    count = 0
    errors = 0

    with open(logfile) as f, open(outfile, "w") as out:
        for line in f:
            msg = parse_line(line)
            if msg is None:
                if line.strip():
                    errors += 1
                continue
            count += 1
            process_message(msg, out)

        out.write(f"\n--- {count} messages parsed, {errors} unparseable lines ---\n")

    print(f"Wrote {count} parsed messages to {outfile}  ({errors} unparseable lines)")


if __name__ == "__main__":
    main()