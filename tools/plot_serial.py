#!/usr/bin/env python3
"""Log and/or plot the device's serial CSV stream.

The firmware prints one CSV row every 500 ms:
    millis,boiler,group,set,boiler_set,duty,z1,z2,boost,offset,state,draw

Capture:
    python tools/plot_serial.py --port /dev/ttyACM0 --log run.csv
Plot a finished log:
    python tools/plot_serial.py run.csv
"""

import argparse
import sys


def capture(port: str, baud: int, log: str) -> None:
    import serial  # pyserial

    with serial.Serial(port, baud, timeout=5) as ser, open(log, "w") as out:
        print(f"logging {port} -> {log}  (Ctrl-C to stop)")
        try:
            while True:
                line = ser.readline().decode(errors="replace").strip()
                if not line:
                    continue
                print(line)
                if line[0].isdigit() or line.startswith("millis"):
                    out.write(line + "\n")
                    out.flush()
        except KeyboardInterrupt:
            print("\nstopped")


def plot(path: str, save: str | None) -> None:
    import matplotlib.pyplot as plt
    import pandas as pd

    df = pd.read_csv(path)
    t = (df["millis"] - df["millis"].iloc[0]) / 60000.0

    fig, (ax1, ax2) = plt.subplots(
        2, 1, sharex=True, figsize=(11, 6), height_ratios=[3, 1.2]
    )
    ax1.plot(t, df["group"], label="group", lw=1.8)
    ax1.plot(t, df["boiler"], label="boiler", lw=1.2)
    ax1.plot(t, df["boiler_set"], label="boiler set", ls="--", lw=1)
    ax1.plot(t, df["set"], label="group set", ls=":", lw=1)
    ax1.set_ylabel("°C")
    ax1.legend(fontsize=8)
    ax1.grid(alpha=0.3)

    ax2.plot(t, df["duty"], lw=1, label="duty")
    ax2.plot(t, df["z2"], lw=1, label="z2")
    ax2.set_xlabel("time [min]")
    ax2.legend(fontsize=8)
    ax2.grid(alpha=0.3)

    fig.suptitle(path)
    fig.tight_layout()
    if save:
        fig.savefig(save, dpi=130)
        print(f"wrote {save}")
    else:
        plt.show()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="?", help="existing log to plot")
    ap.add_argument("--port", help="serial port to capture from")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--log", default="serial.csv", help="capture output file")
    ap.add_argument("--save", help="write PNG instead of showing a window")
    args = ap.parse_args()

    if args.port:
        capture(args.port, args.baud, args.log)
    elif args.csv:
        plot(args.csv, args.save)
    else:
        ap.print_help()
        sys.exit(2)


if __name__ == "__main__":
    main()
