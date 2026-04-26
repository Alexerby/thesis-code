"""
Order book imbalance and volume metrics per cancel event.

Walks every MBO message for the full trading day, maintains running book
state, and emits one row each time a cancel fires — mirroring the logic in
OrderTracker (see tests/test_order_tracker.cpp).

Features emitted per cancel:
  cancel_type       0 = Pure (no prior fill on this order)
                    1 = Fill (order was at least partially filled first)
  cancel_size       shares cancelled
  side              B = bid-side cancel, A = ask-side cancel
  obi_l1            OBI at best bid/ask only
  obi_all           OBI across all visible price levels
  cum_add_bid/ask   cumulative added volume per side up to this event
  cum_cancel_bid/ask  cumulative cancelled volume per side
  cum_trade_vol     cumulative traded volume (fills)

Output: output/MULN_YYYYMMDD/FEATURES.csv  (one file per event date)

Usage:
    python -m analysis.features
    python -m analysis.features --force   # re-export cached MBO CSVs
"""

import argparse
import json
import os
import subprocess
import sys

import pandas as pd
import zoneinfo

EVENTS_FILE = "events.json"
ET = zoneinfo.ZoneInfo("America/New_York")


# ── I/O helpers ───────────────────────────────────────────────────────────────

def export_mbo_csv(dbn_path: str, csv_path: str) -> None:
    os.makedirs(os.path.dirname(csv_path), exist_ok=True)
    subprocess.run(
        ["dbn", "--csv", "--pretty", "--map-symbols", dbn_path, "-o", csv_path],
        check=True,
    )


def load_mbo(csv_path: str) -> pd.DataFrame:
    df = pd.read_csv(csv_path, dtype={"action": str, "side": str})
    df["ts_ns"] = pd.to_datetime(df["ts_recv"], utc=True).astype("int64")
    df["price"] = pd.to_numeric(df["price"], errors="coerce")
    return df


def ns_to_et_str(ns: int) -> str:
    return (
        pd.Timestamp(ns, unit="ns", tz="UTC")
        .tz_convert(ET)
        .strftime("%Y-%m-%d %H:%M:%S.%f ET")
    )


# ── Feature computation ────────────────────────────────────────────────────────

def compute_features(df: pd.DataFrame) -> pd.DataFrame:
    bid_levels: dict[float, int] = {}   # price → aggregate resting size
    ask_levels: dict[float, int] = {}
    # order_id → {price, side, size, had_fill}
    order_map:  dict[int, dict]  = {}

    cum_add_bid = cum_add_ask = 0
    cum_cancel_bid = cum_cancel_ask = 0
    cum_trade_vol = 0

    records: list[dict] = []

    for row in df.itertuples(index=False):
        act  = str(row.action)
        side = str(row.side)
        px   = float(row.price) if row.price == row.price else float("nan")
        sz   = int(row.size)
        oid  = int(row.order_id)
        ts   = int(row.ts_ns)

        if act == "R":
            bid_levels.clear()
            ask_levels.clear()
            order_map.clear()

        elif act == "A" and px == px:
            if oid not in order_map:
                order_map[oid] = {"price": px, "side": side, "size": sz, "had_fill": False}
            else:
                order_map[oid]["size"] += sz   # modify / re-add
            lvl = bid_levels if side == "B" else ask_levels
            lvl[px] = lvl.get(px, 0) + sz
            if side == "B": cum_add_bid += sz
            else:           cum_add_ask += sz

        elif act == "F":
            if oid in order_map:
                o = order_map[oid]
                o["had_fill"] = True
                o["size"] = max(0, o["size"] - sz)
                lvl = bid_levels if o["side"] == "B" else ask_levels
                lvl[o["price"]] = max(0, lvl.get(o["price"], 0) - sz)
                if lvl[o["price"]] == 0:
                    del lvl[o["price"]]
                cum_trade_vol += sz

        elif act == "C":
            if oid not in order_map:
                continue
            o = order_map.pop(oid)
            lvl = bid_levels if o["side"] == "B" else ask_levels
            lvl[o["price"]] = max(0, lvl.get(o["price"], 0) - sz)
            if lvl.get(o["price"], 0) == 0:
                lvl.pop(o["price"], None)

            cancel_type = 1 if o["had_fill"] else 0

            if o["side"] == "B": cum_cancel_bid += sz
            else:                cum_cancel_ask += sz

            # OBI at L1
            bb_vol = bid_levels.get(max(bid_levels), 0) if bid_levels else 0
            ba_vol = ask_levels.get(min(ask_levels), 0) if ask_levels else 0
            d1 = bb_vol + ba_vol
            obi_l1 = (bb_vol - ba_vol) / d1 if d1 > 0 else float("nan")

            # OBI across all levels
            total_bid = sum(bid_levels.values())
            total_ask = sum(ask_levels.values())
            d_all = total_bid + total_ask
            obi_all = (total_bid - total_ask) / d_all if d_all > 0 else float("nan")

            records.append({
                "ts_recv":            ts,
                "ts_recv_et":         ns_to_et_str(ts),
                "order_id":           oid,
                "side":               o["side"],
                "price":              o["price"],
                "cancel_size":        sz,
                "cancel_type":        cancel_type,
                "obi_l1":             obi_l1,
                "obi_all":            obi_all,
                "cum_add_vol_bid":    cum_add_bid,
                "cum_add_vol_ask":    cum_add_ask,
                "cum_cancel_vol_bid": cum_cancel_bid,
                "cum_cancel_vol_ask": cum_cancel_ask,
                "cum_trade_vol":      cum_trade_vol,
            })

    return pd.DataFrame(records)


# ── Entry point ───────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--force", action="store_true", help="Re-export cached MBO CSVs")
    args = ap.parse_args()

    with open(EVENTS_FILE) as f:
        events = json.load(f)["events"]

    for ev in events:
        date      = ev["date"]
        compact   = date.replace("-", "")
        out_dir   = f"output/MULN_{compact}"
        csv_path  = f"{out_dir}/MBO.csv"
        feat_path = f"{out_dir}/FEATURES.csv"
        dbn_path  = ev["dbn"]["event"]

        os.makedirs(out_dir, exist_ok=True)

        if not os.path.exists(dbn_path):
            print(f"  skip  {date} — DBN not found: {dbn_path}")
            continue

        if not os.path.exists(csv_path) or args.force:
            print(f"  export {dbn_path} → {csv_path}")
            export_mbo_csv(dbn_path, csv_path)

        print(f"  features {date} ...", end=" ", flush=True)
        df = load_mbo(csv_path)
        features = compute_features(df)
        features.to_csv(feat_path, index=False)
        print(f"{len(features):,} cancel events → {feat_path}")


if __name__ == "__main__":
    main()
