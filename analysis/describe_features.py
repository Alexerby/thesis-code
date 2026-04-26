"""
Descriptive analysis of extracted features.

Usage:
    python scripts/describe_features.py --features output/MULN_20221025/FEATURES.csv
    python scripts/describe_features.py  # runs all events found in events.json
"""

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime

import pandas as pd

EVENTS_FILE = "events.json"
BINARY = "./build/thesis"
FEATURES = [
    "induced_imbalance",
]


def window_to_ns(date_str, time_str):
    """Convert a date (YYYY-MM-DD) + time (HH:MM:SS) in ET to nanoseconds since epoch."""
    import zoneinfo

    dt_et = datetime.strptime(f"{date_str} {time_str}", "%Y-%m-%d %H:%M:%S")
    dt_et = dt_et.replace(tzinfo=zoneinfo.ZoneInfo("America/New_York"))
    return int(dt_et.timestamp() * 1e9)


def ns_to_et_str(ns_series):
    """Convert a Series of UTC nanoseconds to ET datetime strings."""
    import zoneinfo

    return (
        pd.to_datetime(ns_series, unit="ns", utc=True)
        .dt.tz_convert(zoneinfo.ZoneInfo("America/New_York"))
        .dt.strftime("%Y-%m-%d %H:%M:%S.%f ET")
    )


def load_features(path):
    return pd.read_csv(path)


def analyze_event(df, event):
    date = event["date"]
    win_start = window_to_ns(date, event["window_start"])
    win_end = window_to_ns(date, event["window_end"])

    in_window = df[(df["ts_recv"] >= win_start) & (df["ts_recv"] <= win_end)]
    out_window = df[(df["ts_recv"] < win_start) | (df["ts_recv"] > win_end)]

    print(f"\n{'=' * 60}")
    print(f"  {date}  |  window {event['window_start']} – {event['window_end']} ET")
    print(f"  Total records : {len(df):,}")
    print(f"  In window     : {len(in_window):,}")
    print(f"  Out of window : {len(out_window):,}")
    print(f"{'=' * 60}")

    # TODO: add feature-by-feature analysis here


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--features", help="Path to a single FEATURES.csv")
    args = p.parse_args()

    with open(EVENTS_FILE) as f:
        events_config = json.load(f)

    if not os.path.isfile(BINARY) or not os.access(BINARY, os.X_OK):
        sys.exit(f"Binary not found at {BINARY} — run cmake --build build first.")

    if args.features:
        df = load_features(args.features)
        matched = next(
            (
                e
                for e in events_config["events"]
                if e["date"].replace("-", "") in args.features
            ),
            events_config["events"][0],
        )
        analyze_event(df, matched)
    else:
        for event in events_config["events"]:
            compact = event["date"].replace("-", "")
            event_dir = f"output/MULN_{compact}"
            csv_path = f"{event_dir}/FEATURES.csv"
            dbn_path = event["dbn"]["event"]

            os.makedirs(event_dir, exist_ok=True)

            if not os.path.exists(csv_path):
                if not os.path.exists(dbn_path):
                    print(f"  Skipping {event['date']} — DBN not found: {dbn_path}")
                    continue
                print(f"  Extracting features: {dbn_path}")
                subprocess.run(
                    [
                        BINARY,
                        "extract-features",
                        dbn_path,
                        "--ticker",
                        "MULN",
                        "--output",
                        csv_path,
                    ],
                    check=True,
                )

            df = load_features(csv_path)
            analyze_event(df, event)


if __name__ == "__main__":
    main()
