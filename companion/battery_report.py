#!/usr/bin/env python3
"""Summarize ~/Library/Logs/PassportMic/battery.csv into drain rates.

Each pair of consecutive reports becomes one interval, labelled by the state and
screen level at its start. Intervals with USB present (charging), a gap longer
than three report periods (bridge or link down) or an unread SOC are skipped.

Usage: .venv/bin/python companion/battery_report.py [battery.csv] [--since ISO-TIME]
"""
import argparse
import csv
from collections import defaultdict
from datetime import datetime
from pathlib import Path

DEFAULT_LOG = Path.home() / "Library/Logs/PassportMic/battery.csv"
MAX_GAP_S = 180
CAPACITY_MAH = 500  # CW2017 profile on this board.


def load(path, since=None):
    rows = []
    with Path(path).open(newline="") as handle:
        for row in csv.DictReader(handle):
            when = datetime.fromisoformat(row["time"])
            if since and when < since:
                continue
            soc = float(row["soc"]) if row["soc"] else None
            rows.append((when, soc, row["state"], row["screen"], row["usb"] == "True"))
    return rows


def summarize(rows):
    """Return {(state, screen): [hours, soc_drop]} over the usable intervals."""
    totals = defaultdict(lambda: [0.0, 0.0])
    for (t0, soc0, state, screen, usb0), (t1, soc1, _, _, usb1) in zip(rows, rows[1:]):
        seconds = (t1 - t0).total_seconds()
        if usb0 or usb1 or soc0 is None or soc1 is None or not 0 < seconds <= MAX_GAP_S:
            continue
        totals[(state, screen)][0] += seconds / 3600
        totals[(state, screen)][1] += soc0 - soc1
    return dict(totals)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("path", nargs="?", default=DEFAULT_LOG)
    parser.add_argument("--since", type=datetime.fromisoformat, help="ignore earlier reports")
    args = parser.parse_args()
    since = args.since.astimezone() if args.since and args.since.tzinfo is None else args.since
    rows = load(args.path, since)
    if len(rows) < 2:
        print("Not enough reports yet; the device sends one per minute while linked.")
        return
    first, last = rows[0], rows[-1]
    print(f"Reports: {len(rows)}  from {first[0]:%m-%d %H:%M} to {last[0]:%m-%d %H:%M}")
    if first[1] is not None and last[1] is not None:
        print(f"SOC: {first[1]:.2f}% -> {last[1]:.2f}%")
    print(f"{'state':<10} {'screen':<6} {'hours':>6} {'drop %':>7} {'%/h':>6} {'mA est':>7}")
    for (state, screen), (hours, drop) in sorted(summarize(rows).items()):
        rate = drop / hours if hours else 0.0
        print(f"{state:<10} {screen:<6} {hours:6.2f} {drop:7.2f} {rate:6.2f} {rate * CAPACITY_MAH / 100:7.1f}")
    print("Rates below about 1 hour of data, or within 0.5% of SOC, are noise.")


if __name__ == "__main__":
    main()
