"""
Exploratory spread grid for all MULN days with inexact spoofing windows
(window_exact: false in events.json), focusing on 15:30–16:00 ET.

Usage:
    python -m analysis.plot_late_day
    python -m analysis.plot_late_day --force       # re-export cached CSVs
    python -m analysis.plot_late_day --ref 15:30   # move the reference line
"""

import argparse
import json
import math
import os
import subprocess
import sys
from datetime import datetime

import zoneinfo
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import numpy as np
import pandas as pd

EVENTS_FILE = "events.json"
ET          = zoneinfo.ZoneInfo("America/New_York")

PLOT_START_ET = "15:30:00"  # start of every panel
PLOT_END_ET   = "16:00:00"  # end of every panel
REF_ET        = "15:45:00"  # dashed reference line (suspected spoofing onset)
NCOLS         = 4


# ── Helpers (duplicated from plot_cases.py intentionally — scripts are independent) ──

def et_to_ns(date: str, time: str) -> int:
    dt = datetime.strptime(f"{date} {time}", "%Y-%m-%d %H:%M:%S")
    return int(dt.replace(tzinfo=ET).timestamp() * 1e9)


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


def reconstruct_bbo(df: pd.DataFrame, start_ns: int, end_ns: int) -> pd.DataFrame:
    bid_lvl: dict[float, int] = {}
    ask_lvl: dict[float, int] = {}
    omap: dict[int, dict] = {}
    out: list[dict] = []

    for row in df.itertuples(index=False):
        act  = row.action
        side = row.side
        px   = row.price
        sz   = int(row.size)
        oid  = row.order_id
        ts   = int(row.ts_ns)

        if act == "R":
            bid_lvl.clear(); ask_lvl.clear(); omap.clear()
        elif act == "A" and px == px:
            omap[oid] = {"price": px, "side": side, "size": sz}
            if side == "B":   bid_lvl[px] = bid_lvl.get(px, 0) + sz
            elif side == "A": ask_lvl[px] = ask_lvl.get(px, 0) + sz
        elif act in ("C", "F"):
            if oid in omap:
                o = omap[oid]
                lvl = bid_lvl if o["side"] == "B" else ask_lvl
                lvl[o["price"]] = lvl.get(o["price"], 0) - sz
                if lvl.get(o["price"], 0) <= 0:
                    lvl.pop(o["price"], None)
                o["size"] -= sz
                if o["size"] <= 0:
                    omap.pop(oid, None)

        if start_ns <= ts <= end_ns:
            bb = max(bid_lvl) if bid_lvl else float("nan")
            ba = min(ask_lvl) if ask_lvl else float("nan")
            out.append({"ts_ns": ts, "bid": bb, "ask": ba})

    return pd.DataFrame(out)


# ── Plotting ──────────────────────────────────────────────────────────────────

def style_ax(ax):
    ax.set_facecolor("#FFFFFF")
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.spines["left"].set_color("#888888")
    ax.spines["bottom"].set_color("#888888")
    ax.tick_params(colors="#444444", labelsize=7)
    ax.grid(axis="y", alpha=0.15, color="#000000", lw=0.5, zorder=0)


def plot_panel(ax, bbo: pd.DataFrame, date: str,
               plot_start_ns: int, ref_ns: int, total_min: float) -> float | None:
    ax.set_title(date, fontsize=8, pad=4, loc="left",
                 color="#222222", fontweight="bold")

    if bbo.empty:
        ax.text(0.5, 0.5, "no data", ha="center", va="center",
                transform=ax.transAxes, color="#aaaaaa", fontsize=8)
        style_ax(ax)
        return None

    bbo = bbo.copy()
    bbo["t"] = (bbo["ts_ns"] - plot_start_ns) / 60e9  # minutes from plot start

    # mid0: last BBO before the reference line; fall back to first available
    pre = bbo[bbo["ts_ns"] < ref_ns].dropna(subset=["bid", "ask"])
    if not pre.empty:
        r = pre.iloc[-1]
        mid0 = (r["bid"] + r["ask"]) / 2.0
    else:
        valid = bbo.dropna(subset=["bid", "ask"])
        if valid.empty:
            style_ax(ax)
            return None
        r = valid.iloc[0]
        mid0 = (r["bid"] + r["ask"]) / 2.0

    bid_s = ((bbo.set_index("t")["bid"] - mid0) / mid0 * 100).ffill()
    ask_s = ((bbo.set_index("t")["ask"] - mid0) / mid0 * 100).ffill()

    ref_min = (ref_ns - plot_start_ns) / 60e9
    ax.axvspan(ref_min, total_min, alpha=0.07, color="#D4AF37", lw=0, zorder=0)
    ax.axvline(ref_min, color="#D4AF37", alpha=0.55, lw=0.9, ls="--", zorder=4)

    ax.fill_between(bid_s.index, bid_s.values, -100,
                    step="post", color="#27ae60", alpha=0.15, zorder=1)
    ax.fill_between(ask_s.index, ask_s.values,  100,
                    step="post", color="#c0392b", alpha=0.15, zorder=1)
    ax.step(bid_s.index, bid_s.values, where="post", color="#1e8449", lw=0.9, zorder=2)
    ax.step(ask_s.index, ask_s.values, where="post", color="#922b21", lw=0.9, zorder=2)

    ax.set_xlim(0, total_min)
    ax.yaxis.set_major_formatter(mticker.PercentFormatter(decimals=1))
    style_ax(ax)
    return mid0


def set_x_ticks(ax, plot_start_et: str, total_min: float, step_min: int = 10):
    """Label X ticks as ET wall-clock times (HH:MM)."""
    h0, m0, _ = (int(x) for x in plot_start_et.split(":"))
    ticks = list(range(0, int(total_min) + 1, step_min))
    labels = []
    for t in ticks:
        total = h0 * 60 + m0 + t
        labels.append(f"{total // 60}:{total % 60:02d}")
    ax.set_xticks(ticks)
    ax.set_xticklabels(labels, fontsize=6)


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="Re-export cached CSVs")
    ap.add_argument("--ref", default=REF_ET,
                    help="Reference line time in HH:MM:SS ET (default %(default)s)")
    args = ap.parse_args()

    with open(EVENTS_FILE) as f:
        all_events = json.load(f)["events"]

    candidates = [
        (e["date"], e["dbn"]["event"])
        for e in all_events
        if not e.get("window_exact") and os.path.exists(e["dbn"]["event"])
    ]

    if not candidates:
        sys.exit("No inexact-window events found in events.json.")

    print(f"Found {len(candidates)} dates to plot.")

    os.makedirs("output", exist_ok=True)

    ref_time = args.ref if ":" in args.ref else args.ref + ":00"
    total_min = (
        sum(int(x) * f for x, f in zip(PLOT_END_ET.split(":"),   [60, 1, 1/60])) -
        sum(int(x) * f for x, f in zip(PLOT_START_ET.split(":"), [60, 1, 1/60]))
    )

    # ── 1. Build BBO for each candidate ──────────────────────────────────────
    panels = []
    for date, dbn_path in candidates:
        compact  = date.replace("-", "")
        out_dir  = f"output/MULN_{compact}"
        csv_path = f"{out_dir}/MBO.csv"

        if not os.path.exists(csv_path) or args.force:
            print(f"  export  {dbn_path} → {csv_path}")
            export_mbo_csv(dbn_path, csv_path)

        print(f"  bbo     {date} ...", end=" ", flush=True)
        df = load_mbo(csv_path)

        start_ns = et_to_ns(date, PLOT_START_ET)
        end_ns   = et_to_ns(date, PLOT_END_ET)
        ref_ns   = et_to_ns(date, ref_time)

        bbo = reconstruct_bbo(df, start_ns, end_ns)
        print(f"{len(bbo):,} msgs")
        panels.append({"date": date, "bbo": bbo, "start_ns": start_ns, "ref_ns": ref_ns})

    # ── 2. Shared Y limits (1.5–98.5 percentile across all panels) ────────────
    all_pct: list[float] = []
    for p in panels:
        bbo = p["bbo"]
        if bbo.empty:
            continue
        valid = bbo.dropna(subset=["bid", "ask"])
        if valid.empty:
            continue
        pre = valid[valid["ts_ns"] < p["ref_ns"]]
        if not pre.empty:
            mid0 = (pre.iloc[-1]["bid"] + pre.iloc[-1]["ask"]) / 2.0
        else:
            mid0 = (valid.iloc[0]["bid"] + valid.iloc[0]["ask"]) / 2.0
        all_pct.extend(((valid["bid"] - mid0) / mid0 * 100).tolist())
        all_pct.extend(((valid["ask"] - mid0) / mid0 * 100).tolist())

    if all_pct:
        y_lo = float(np.nanpercentile(all_pct, 1.5))
        y_hi = float(np.nanpercentile(all_pct, 98.5))
        pad  = (y_hi - y_lo) * 0.08
        y_lo -= pad; y_hi += pad
    else:
        y_lo, y_hi = -2.0, 2.0

    # ── 3. Grid figure ────────────────────────────────────────────────────────
    n      = len(panels)
    ncols  = min(NCOLS, n)
    nrows  = math.ceil(n / ncols)

    fig, axes = plt.subplots(nrows, ncols,
                              figsize=(ncols * 3.5, nrows * 2.8),
                              sharey=True, sharex=False,
                              facecolor="#FFFFFF")
    fig.subplots_adjust(hspace=0.5, wspace=0.08,
                        left=0.06, right=0.98, top=0.93, bottom=0.06)

    ax_flat = axes.flat if n > 1 else [axes]
    for ax, panel in zip(ax_flat, panels):
        plot_panel(ax, panel["bbo"], panel["date"],
                   panel["start_ns"], panel["ref_ns"], total_min)
        set_x_ticks(ax, PLOT_START_ET, total_min)
        ax.set_ylim(y_lo, y_hi)

    # Hide unused axes
    for ax in list(ax_flat)[n:]:
        ax.set_visible(False)

    # Y-axis labels on leftmost column only
    for r in range(nrows):
        ax = axes[r, 0] if nrows > 1 else axes[0]
        ax.set_ylabel("Δ vs. pre-ref mid (%)", fontsize=7, color="#333333")

    fig.suptitle(
        f"MULN — Late-Day BBO ({PLOT_START_ET[:5]}–{PLOT_END_ET[:5]} ET)  "
        f"| dashed line = {ref_time[:5]} ET",
        fontsize=10, fontweight="bold", y=0.98, color="#111111",
    )

    out_png = "output/SPREAD_ALL_DAYS.png"
    fig.savefig(out_png, dpi=180, bbox_inches="tight")
    print(f"\nSaved: {out_png}")


if __name__ == "__main__":
    main()
