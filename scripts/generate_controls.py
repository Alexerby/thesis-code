"""
Generates scripts/fetch_controls.sh — a batch fetch script for MULN control days.

For each calendar week that contains a spoofing date, finds non-spoofing
trading days in the same week (excluding ±1 calendar day around any spoofing
date). Falls back to adjacent weeks when a week is fully blocked.

Targets 2 control days per spoofing week. Fetches only 15:00–16:00 ET
(the closing window) to keep data costs minimal.

Usage:
    python scripts/generate_controls.py          # preview + write fetch script
    python scripts/generate_controls.py --dry-run
"""

import argparse
import json
import os
import zoneinfo
from datetime import date, datetime, timedelta

EVENTS_FILE = "events.json"
OUT_SCRIPT  = "scripts/fetch_controls.sh"
BINARY      = "./build/thesis"
DATA_DIR    = "data/CONTROL"
ET          = zoneinfo.ZoneInfo("America/New_York")
UTC         = zoneinfo.ZoneInfo("UTC")
CONTROLS_PER_WEEK = 2

# NYSE holidays covering the 2022–2023 dataset range
NYSE_HOLIDAYS: set[date] = {
    date(2022, 1, 17),  date(2022, 2, 21),  date(2022, 4, 15),
    date(2022, 5, 30),  date(2022, 6, 20),  date(2022, 7, 4),
    date(2022, 9, 5),   date(2022, 11, 24), date(2022, 12, 26),
    date(2023, 1, 2),   date(2023, 1, 16),  date(2023, 2, 20),
    date(2023, 4, 7),   date(2023, 5, 29),  date(2023, 6, 19),
    date(2023, 7, 4),   date(2023, 9, 4),   date(2023, 11, 23),
    date(2023, 12, 25),
}


def is_trading_day(d: date) -> bool:
    return d.weekday() < 5 and d not in NYSE_HOLIDAYS


def week_of(d: date) -> date:
    """Return the Monday of d's calendar week."""
    return d - timedelta(days=d.weekday())


def trading_days_in_week(monday: date) -> list[date]:
    return [monday + timedelta(days=i) for i in range(5)
            if is_trading_day(monday + timedelta(days=i))]


def et_window_utc(d: date, start="15:00:00", end="16:00:00") -> tuple[str, str]:
    """Convert a 15:00–16:00 ET window on date d to UTC ISO strings."""
    def to_utc(time_str):
        t = datetime.strptime(time_str, "%H:%M:%S").time()
        local = datetime.combine(d, t, tzinfo=ET)
        return local.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")
    return to_utc(start), to_utc(end)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true",
                    help="Print the plan without writing the fetch script")
    args = ap.parse_args()

    with open(EVENTS_FILE) as f:
        events = json.load(f)["events"]

    spoofing_dates = {date.fromisoformat(e["date"]) for e in events}

    # Exclusion zone: every spoofing date and the calendar day on each side
    excluded: set[date] = set()
    for d in spoofing_dates:
        for delta in (-1, 0, 1):
            excluded.add(d + timedelta(days=delta))

    # Group spoofing dates by week, then find control candidates
    # week_monday → list of control dates
    week_controls: dict[date, list[date]] = {}

    for monday in sorted({week_of(d) for d in spoofing_dates}):
        picked: list[date] = []

        # Search same week, then ±1, ±2, ±3 weeks until we have enough
        for offset_weeks in range(0, 4):
            for sign in ([0] if offset_weeks == 0 else [-1, 1]):
                candidate_monday = monday + timedelta(weeks=offset_weeks * sign)
                for d in trading_days_in_week(candidate_monday):
                    if d not in excluded and d not in picked:
                        picked.append(d)
                if len(picked) >= CONTROLS_PER_WEEK:
                    break
            if len(picked) >= CONTROLS_PER_WEEK:
                break

        week_controls[monday] = picked[:CONTROLS_PER_WEEK]

    # Unique control dates (a day may serve as control for multiple weeks)
    all_controls: set[date] = {d for days in week_controls.values() for d in days}

    # ── Print plan ────────────────────────────────────────────────────────────
    print(f"\n{'Week':12}  {'Spoofing dates':<35}  Controls")
    print("─" * 75)
    for monday in sorted(week_controls):
        spoof_in_week = sorted(d for d in spoofing_dates if week_of(d) == monday)
        controls      = sorted(week_controls[monday])
        spoof_str   = ", ".join(str(d) for d in spoof_in_week)
        control_str = ", ".join(str(d) for d in controls) or "⚠ none found"
        print(f"{str(monday):12}  {spoof_str:<35}  {control_str}")

    print(f"\n  {len(spoofing_dates)} spoofing dates across "
          f"{len(week_controls)} weeks → "
          f"{len(all_controls)} unique control days")

    if args.dry_run:
        return

    # ── Write fetch script ────────────────────────────────────────────────────
    lines = [
        "#!/usr/bin/env bash",
        "set -euo pipefail",
        f'BINARY="{BINARY}"',
        f'mkdir -p "{DATA_DIR}"',
        "",
    ]

    for ctrl_date in sorted(all_controls):
        compact   = ctrl_date.strftime("%Y%m%d")
        out_path  = f"{DATA_DIR}/MULN_{compact}_ctrl.dbn.zst"
        start_utc, end_utc = et_window_utc(ctrl_date)

        lines += [
            f'if [[ -f "{out_path}" ]]; then',
            f'  echo "  skip  {ctrl_date} — already exists"',
            f'else',
            f'  echo "  fetch {ctrl_date}"',
            f'  "$BINARY" databento-fetch \\',
            f'    --symbols MULN \\',
            f'    --start {start_utc} \\',
            f'    --end   {end_utc} \\',
            f'    --output "{out_path}" \\',
            f'    --yes',
            f'fi',
            "",
        ]

    lines.append('echo "Done. Fetched closing-window data for all control dates."')

    with open(OUT_SCRIPT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os.chmod(OUT_SCRIPT, 0o755)
    print(f"\nWrote {OUT_SCRIPT}")


if __name__ == "__main__":
    main()
