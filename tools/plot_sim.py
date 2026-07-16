#!/usr/bin/env python3
"""Plot a simulator CSV (see sim/sim_main.cpp).

Usage:
    pio run -e native
    .pio/build/native/program --scenario full --csv out.csv
    python tools/plot_sim.py out.csv [--save out.png]
"""

import argparse
import pathlib

import matplotlib
import matplotlib.pyplot as plt
import pandas as pd


def show_or_save(fig, save: str | None, csv_path: str) -> None:
    """plt.show(), unless there is no GUI backend (headless/WSL without a
    display toolkit) — then fall back to writing a PNG next to the CSV."""
    if save:
        fig.savefig(save, dpi=130)
        print(f"wrote {save}")
        return
    if matplotlib.get_backend().lower() in ("agg", "pdf", "ps", "svg", "template"):
        out = str(pathlib.Path(csv_path).with_suffix(".png"))
        fig.savefig(out, dpi=130)
        print(f"no interactive matplotlib backend available -> wrote {out}")
        print("(for live windows: pip install PyQt6)")
        return
    plt.show()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--save", help="write PNG instead of showing a window")
    args = ap.parse_args()

    df = pd.read_csv(args.csv)
    t = df["t"] / 60.0  # minutes

    fig, (ax1, ax2, ax3) = plt.subplots(
        3, 1, sharex=True, figsize=(11, 8), height_ratios=[3, 1.2, 1.2]
    )

    ax1.plot(t, df["group_sens"], label="group (sensed)", lw=1.8)
    ax1.plot(t, df["boiler_sens"], label="boiler (sensed)", lw=1.2)
    ax1.plot(t, df["brass"], label="brass (truth)", lw=0.8, alpha=0.6)
    ax1.plot(t, df["water"], label="water (truth)", lw=0.8, alpha=0.6)
    ax1.plot(t, df["boiler_set"], label="boiler setpoint", ls="--", lw=1)
    ax1.plot(t, df["group_set"], label="group setpoint", ls=":", lw=1)
    ax1.set_ylabel("°C")
    ax1.legend(loc="lower right", fontsize=8, ncols=2)
    ax1.grid(alpha=0.3)

    ax2.plot(t, df["duty"], lw=1)
    draws = df[df["draw"] == 1]
    if not draws.empty:
        ax2.scatter(draws["t"] / 60.0, draws["duty"], s=4, label="draw detected")
        ax2.legend(fontsize=8)
    ax2.set_ylabel("duty")
    ax2.set_ylim(-0.05, 1.05)
    ax2.grid(alpha=0.3)

    ax3.plot(t, df["z2"], lw=1, label="z2 (disturbance)")
    ax3.plot(t, df["boost"], lw=1, label="boost °C")
    ax3.plot(t, df["offset"], lw=1, label="offset_ss °C")
    ax3.set_xlabel("time [min]")
    ax3.legend(fontsize=8)
    ax3.grid(alpha=0.3)

    fig.suptitle(args.csv)
    fig.tight_layout()
    show_or_save(fig, args.save, args.csv)


if __name__ == "__main__":
    main()
