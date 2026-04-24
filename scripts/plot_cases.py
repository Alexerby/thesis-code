"""
Publication-quality spread plots for the four MULN spoofing events.

For each event in events.json:
  1. Export DBN file → MBO CSV (cached at output/MULN_YYYYMMDD/MBO.csv).
  2. Reconstruct BBO (best bid/offer) by walking the full order book.
  3. Plot ±CONTEXT_MIN around the manipulation window.

All four panels share the same Y axis (price relative to mid at window open).

Usage:
    python scripts/plot_cases.py
    python scripts/plot_cases.py --force      # re-export cached CSVs
    python scripts/plot_cases.py --context 5  # ±5-minute window instead
"""

import argparse
import json
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
CONFIG_FILE = "config.json"
CONTEXT_MIN = 10  # ±N minutes around the event

ET = zoneinfo.ZoneInfo("America/New_York")


# ── Helpers ───────────────────────────────────────────────────────────────────

def et_to_ns(date_str: str, time_str: str) -> int:
    dt = datetime.strptime(f"{date_str} {time_str}", "%Y-%m-%d %H:%M:%S")
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


def reconstruct_bbo(df: pd.DataFrame, plot_start_ns: int, plot_end_ns: int) -> pd.DataFrame:
    """
    Walk every row in df maintaining a full order book.
    Emit a BBO snapshot for each row that falls inside [plot_start_ns, plot_end_ns].

    Returns columns: ts_ns, bid, ask, action, side, price, size.
    """
    bid_lvl: dict[float, int] = {}  # price → aggregate size
    ask_lvl: dict[float, int] = {}
    omap: dict[int, dict] = {}       # order_id → {price, side, size}
    out: list[dict] = []

    for row in df.itertuples(index=False):
        act  = row.action
        side = row.side
        px   = row.price   # float or NaN
        sz   = int(row.size)
        oid  = row.order_id
        ts   = int(row.ts_ns)

        if act == "R":
            bid_lvl.clear()
            ask_lvl.clear()
            omap.clear()

        elif act == "A" and px == px:   # NaN check: px != px if NaN
            omap[oid] = {"price": px, "side": side, "size": sz}
            if side == "B":
                bid_lvl[px] = bid_lvl.get(px, 0) + sz
            elif side == "A":
                ask_lvl[px] = ask_lvl.get(px, 0) + sz

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

        if plot_start_ns <= ts <= plot_end_ns:
            bb = max(bid_lvl) if bid_lvl else float("nan")
            ba = min(ask_lvl) if ask_lvl else float("nan")
            out.append(
                {"ts_ns": ts, "bid": bb, "ask": ba,
                 "action": act, "side": side, "price": px, "size": sz}
            )

    return pd.DataFrame(out)


# ── Plotting ──────────────────────────────────────────────────────────────────

def style_ax(ax):
    ax.set_facecolor("#FFFFFF")
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.spines["left"].set_color("#888888")
    ax.spines["bottom"].set_color("#888888")
    ax.tick_params(colors="#444444", labelsize=8)
    ax.yaxis.label.set_color("#333333")
    ax.xaxis.label.set_color("#333333")
    ax.grid(axis="y", alpha=0.15, color="#000000", lw=0.5, zorder=0)


def plot_panel(ax, bbo: pd.DataFrame, event: dict,
               win_dur_min: float, context_min: int,
               mid0: float, show_legend: bool) -> None:
    bbo = bbo.copy()
    bbo["t"] = (bbo["ts_ns"] - event["_win_start_ns"]) / 60e9

    # Calculate percentage change relative to mid0
    bid_s = ((bbo.set_index("t")["bid"] - mid0) / mid0 * 100).ffill()
    ask_s = ((bbo.set_index("t")["ask"] - mid0) / mid0 * 100).ffill()

    # Manipulation window shading: Subtle Amber
    ax.axvspan(0.0, win_dur_min, alpha=0.12, color="#D4AF37",
               lw=0, zorder=0, label="Manipulation window")
    ax.axvline(0.0,         color="#D4AF37", alpha=0.5, lw=0.9, ls="--", zorder=4)
    ax.axvline(win_dur_min, color="#D4AF37", alpha=0.5, lw=0.9, ls="--", zorder=4)

    # Filled areas (filling to extremes)
    ax.fill_between(bid_s.index, bid_s.values, -100,
                    step="post", color="#27ae60", alpha=0.15, zorder=1, label="Best bid")
    ax.fill_between(ask_s.index, ask_s.values, 100,
                    step="post", color="#c0392b", alpha=0.15, zorder=1, label="Best ask")

    # Sharp step lines
    ax.step(bid_s.index, bid_s.values, where="post",
            color="#1e8449", lw=1.2, zorder=2)
    ax.step(ask_s.index, ask_s.values, where="post",
            color="#922b21", lw=1.2, zorder=2)

    ax.set_xlim(-context_min, win_dur_min + context_min)
    ax.set_title(
        f"¶{event['complaint_paragraph']} — {event['date']}  "
        f"({event['window_start']}–{event['window_end']} ET)",
        fontsize=9, pad=8, loc="left", color="#222222", fontweight="bold"
    )
    ax.set_xlabel("Minutes from manipulation window open", fontsize=8)
    ax.xaxis.set_major_locator(mticker.MultipleLocator(2))
    ax.xaxis.set_minor_locator(mticker.MultipleLocator(1))
    
    # Format Y axis to show percent sign
    ax.yaxis.set_major_formatter(mticker.PercentFormatter(decimals=1))
    
    style_ax(ax)

    if show_legend:
        leg = ax.legend(fontsize=7.5, loc="upper left", framealpha=0.9,
                        edgecolor="#dddddd")
        for text in leg.get_texts():
            text.set_color("#333333")


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force",   action="store_true", help="Re-export cached CSVs")
    ap.add_argument("--context", type=int, default=CONTEXT_MIN,
                    help="Minutes of context on each side (default %(default)s)")
    args = ap.parse_args()
    context_min = args.context

    with open(EVENTS_FILE) as f:
        events = json.load(f)["events"]

    os.makedirs("output", exist_ok=True)

    # ── 1. Load / export data for each event ─────────────────────────────────
    panels = []
    for ev in events:
        compact  = ev["date"].replace("-", "")
        out_dir  = f"output/MULN_{compact}"
        csv_path = f"{out_dir}/MBO.csv"

        if not os.path.exists(csv_path) or args.force:
            dbn_path = ev["dbn"]["event"]
            if not os.path.exists(dbn_path):
                sys.exit(f"DBN not found: {dbn_path}")
            print(f"Exporting {dbn_path} → {csv_path} ...", flush=True)
            export_mbo_csv(dbn_path, csv_path)

        print(f"Building BBO for {ev['date']} ...", end=" ", flush=True)
        df = load_mbo(csv_path)

        win_start_ns = et_to_ns(ev["date"], ev["window_start"])
        win_end_ns   = et_to_ns(ev["date"], ev["window_end"])
        ctx_ns       = context_min * 60 * 1_000_000_000

        bbo = reconstruct_bbo(df, win_start_ns - ctx_ns, win_end_ns + ctx_ns)
        print(f"{len(bbo):,} messages in plot window")

        # mid0 = midprice from the last BBO snapshot before the window opens
        pre = bbo[bbo["ts_ns"] < win_start_ns].dropna(subset=["bid", "ask"])
        if not pre.empty:
            last = pre.iloc[-1]
            mid0 = (last["bid"] + last["ask"]) / 2.0
        else:
            first = bbo.dropna(subset=["bid", "ask"]).iloc[0]
            mid0 = (first["bid"] + first["ask"]) / 2.0

        ev["_win_start_ns"] = win_start_ns
        ev["_win_end_ns"]   = win_end_ns
        panels.append({"ev": ev, "bbo": bbo, "mid0": mid0,
                        "win_dur_min": (win_end_ns - win_start_ns) / 60e9})

    # ── 2. Shared Y limits ────────────────────────────────────────────────────
    all_vals = []
    for p in panels:
        bbo = p["bbo"]
        mid0 = p["mid0"]
        # Use percentage values for global limit calculation
        all_vals.extend(((bbo["bid"] - mid0) / mid0 * 100).dropna().tolist())
        all_vals.extend(((bbo["ask"] - mid0) / mid0 * 100).dropna().tolist())

    y_lo = float(np.nanpercentile(all_vals, 1.5))
    y_hi = float(np.nanpercentile(all_vals, 98.5))
    pad  = (y_hi - y_lo) * 0.05
    y_lo -= pad
    y_hi += pad

    # ── 3. Figure ─────────────────────────────────────────────────────────────
    fig, axes = plt.subplots(2, 2, figsize=(13, 8),
                              sharey=True, sharex=False,
                              facecolor="#FFFFFF")
    fig.subplots_adjust(hspace=0.42, wspace=0.06, left=0.07, right=0.97,
                        top=0.91, bottom=0.09)

    for ax, panel in zip(axes.flat, panels):
        plot_panel(ax, panel["bbo"], panel["ev"],
                   panel["win_dur_min"], context_min,
                   panel["mid0"], show_legend=(ax is axes[0, 0]))
        ax.set_ylim(y_lo, y_hi)

    axes[0, 0].set_ylabel("Price change vs. pre-window mid (%)", fontsize=8, color="#333333")
    axes[1, 0].set_ylabel("Price change vs. pre-window mid (%)", fontsize=8, color="#333333")

    fig.suptitle(
        "Best Bid/Offer Around Confirmed Spoofing Windows — MULN (2022–2023)",
        fontsize=11, fontweight="bold", y=0.97, color="#000000"
    )

    # Determine final output path from config.json or default to local output/
    save_dir = "output"
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE) as f:
                cfg = json.load(f)
                save_dir = cfg.get("tex_output_dir", "output")
        except Exception as e:
            print(f"Warning: Failed to load {CONFIG_FILE}: {e}")

    os.makedirs(save_dir, exist_ok=True)
    out_png = os.path.join(save_dir, "SPREAD_CASES.png")
    fig.savefig(out_png, dpi=200, bbox_inches="tight")
    print(f"\nSaved: {out_png}")


if __name__ == "__main__":
    main()
