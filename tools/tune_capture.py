#!/usr/bin/env python3
"""Record the device's serial stream into a capture file and drive tuning
experiments (see docs/tuning-hardware.md).

Record a session (Ctrl-C to stop):
    python tools/tune_capture.py --port /dev/ttyACM0 --name cold_start

Run the identification recipe and record it:
    python tools/tune_capture.py --port /dev/ttyACM0 --name ident --recipe id

One-shot command:
    python tools/tune_capture.py --port /dev/ttyACM0 --send "set cap 0.3"

While recording, type into the terminal:
    m espresso        -> sends `mark espresso` (label live scenarios)
    s kboost 2.5      -> sends `set kboost 2.5`
    g                 -> sends `get`
    anything else     -> sent verbatim (e.g. `id duty 0.4 120`)

Captures land in captures/<YYYYMMDD-HHMMSS>-<name>.log and contain the CSV
rows plus all #PARAMS/#EVT/#MARK/#OK/#ERR lines — feed them to fit_model.py.
"""

import argparse
import datetime
import pathlib
import select
import sys
import time

import serial  # pyserial

# The identification recipe: (wait_seconds_before, command). Durations must
# stay inside the safety envelope — 40% duty from a warm-but-cooling boiler
# cannot reach the 105 C trip. Matches the sim's `ident` scenario, which is
# what fit_model.py is validated against in CI.
RECIPES = {
    "id": [
        (5, "get"),
        (120, "id off 420"),     # settle window first, then cool
        (425, "id duty 0.4 120"),
        (125, "id off 180"),
        (185, "get"),
        (300, None),             # tail: back in regulation
    ],
}


def open_capture(name: str) -> pathlib.Path:
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = pathlib.Path("captures") / f"{ts}-{name}.log"
    path.parent.mkdir(exist_ok=True)
    return path


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--name", default="session")
    ap.add_argument("--recipe", choices=sorted(RECIPES))
    ap.add_argument("--send", help="send one command, print replies for 3 s, exit")
    ap.add_argument("--ambient", type=float, help="ambient temp C (skips the prompt)")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.2)

    if args.send:
        ser.write((args.send + "\n").encode())
        end = time.monotonic() + 3.0
        while time.monotonic() < end:
            line = ser.readline().decode(errors="replace").strip()
            if line:
                print(line)
        return

    ambient = args.ambient
    if ambient is None:
        ambient = float(input("ambient temperature [C]: ").strip() or "20")

    path = open_capture(args.name)
    out = open(path, "w")
    out.write(f"#AMBIENT {ambient:.1f}\n")
    print(f"recording -> {path}  (Ctrl-C to stop)")

    recipe = list(RECIPES[args.recipe]) if args.recipe else []
    next_step_at = time.monotonic() + (recipe[0][0] if recipe else 0)

    def send(cmd: str) -> None:
        print(f">> {cmd}")
        ser.write((cmd + "\n").encode())

    try:
        while True:
            # device -> file (+echo)
            line = ser.readline().decode(errors="replace").strip()
            if line:
                print(line)
                out.write(line + "\n")
                out.flush()

            # recipe steps
            if recipe and time.monotonic() >= next_step_at:
                _, cmd = recipe.pop(0)
                if cmd:
                    send(cmd)
                if recipe:
                    next_step_at = time.monotonic() + recipe[0][0]
                else:
                    print("recipe finished — Ctrl-C when you have enough tail")

            # keyboard -> device
            if select.select([sys.stdin], [], [], 0)[0]:
                user = sys.stdin.readline().strip()
                if not user:
                    continue
                if user.startswith("m "):
                    send("mark " + user[2:])
                elif user.startswith("s "):
                    send("set " + user[2:])
                elif user == "g":
                    send("get")
                else:
                    send(user)
    except KeyboardInterrupt:
        print(f"\nstopped — capture in {path}")
        print(f"next: python tools/fit_model.py {path}")
    finally:
        out.close()
        ser.close()


if __name__ == "__main__":
    main()
