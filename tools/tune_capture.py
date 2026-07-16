#!/usr/bin/env python3
"""Record the device's serial stream into a capture file and drive tuning
experiments (see docs/tuning-hardware.md).

Record a session (Ctrl-C to stop, or --duration to auto-stop):
    python tools/tune_capture.py --port /dev/ttyACM0 --name cold_start

Run the identification recipe and record it:
    python tools/tune_capture.py --port /dev/ttyACM0 --name ident --recipe id

One-shot command:
    python tools/tune_capture.py --port /dev/ttyACM0 --send "set cap 0.3"

While recording, type into the terminal (interactive sessions only):
    m espresso        -> sends `mark espresso` (label live scenarios)
    s kboost 2.5      -> sends `set kboost 2.5`
    g                 -> sends `get`
    anything else     -> sent verbatim (e.g. `id duty 0.4 120`)

Wizard / background operation:
    --duration 600        auto-stop after 10 minutes
    --ctl <path>          poll a sidecar control file (default <capture>.ctl):
                          every NEW line appended to it is sent to the device
                          verbatim; the literal line STOP ends the capture.
                          This is how /tune-wizard injects marks and commands
                          while you are at the machine.
    --status-json <path>  write {rows, seconds, last_state, fault_seen} on exit
    --replay <file>       stream an existing log through the identical code
                          path instead of a serial port (CI / dry runs)

Captures land in captures/<YYYYMMDD-HHMMSS>-<name>.log and contain the CSV
rows plus all #PARAMS/#EVT/#MARK/#OK/#ERR lines — feed them to fit_model.py.
"""

import argparse
import datetime
import json
import pathlib
import select
import sys
import time

# The identification recipe: (wait_seconds_before, command). Durations must
# stay inside the safety envelope — the full-power lag pulse fires from the
# coolest point (~78 C after 7 min of cooling) and the 40% step from ~77 C,
# so neither can approach the 105 C trip. Matches the sim's `ident` scenario,
# which is what fit_model.py is validated against in CI.
#
# Segment roles for fit_model.py:
#   id off 420        cooling         -> C_from_cooling, and the cool-down
#   id duty 1.0 30    sharp lag edge  -> dead time L + sensor tau
#   id duty 0.4 120   moderate step   -> C_from_step
RECIPES = {
    "id": [
        (5, "get"),
        (120, "id off 420"),      # settle window first, then cool
        (425, "id duty 1.0 30"),  # lag-measurement pulse (exactly timestamped)
        (35, "id off 180"),
        (185, "id duty 0.4 120"),
        (125, "id off 180"),
        (185, "get"),
        (300, None),              # tail: back in regulation
    ],
}


class SerialSource:
    def __init__(self, port: str, baud: int):
        import serial  # pyserial, only needed for real hardware

        self.ser = serial.Serial(port, baud, timeout=0.2)

    def readline(self) -> str:
        return self.ser.readline().decode(errors="replace").strip()

    def write(self, cmd: str) -> None:
        self.ser.write((cmd + "\n").encode())

    def close(self) -> None:
        self.ser.close()


class ReplaySource:
    """Streams an existing log through the same code path. Commands that the
    device would echo (#MARK/#OK) are synthesized so ctl-file injection can be
    exercised without hardware."""

    def __init__(self, path: str):
        self.lines = open(path).read().splitlines()
        self.idx = 0
        self.injected = []
        self.last_millis = 0

    def readline(self) -> str:
        if self.injected:
            return self.injected.pop(0)
        if self.idx >= len(self.lines):
            return ""
        line = self.lines[self.idx]
        self.idx += 1
        if line and line[0].isdigit():
            try:
                self.last_millis = int(line.split(",", 1)[0])
            except ValueError:
                pass  # digit-leading but not a CSV row; pass through as-is
        return line

    @property
    def exhausted(self) -> bool:
        return self.idx >= len(self.lines) and not self.injected

    def write(self, cmd: str) -> None:
        if cmd.startswith("mark "):
            self.injected.append(f"#MARK {self.last_millis} {cmd[5:]}")
        elif cmd.startswith("set "):
            self.injected.append(f"#OK {self.last_millis} {cmd}")

    def close(self) -> None:
        pass


class CtlFile:
    """Tail a sidecar control file; each new full line is one command."""

    def __init__(self, path: pathlib.Path):
        self.path = path
        self.offset = 0

    def poll(self) -> list[str]:
        try:
            with open(self.path) as f:
                f.seek(self.offset)
                chunk = f.read()
        except FileNotFoundError:
            return []
        if not chunk.endswith("\n"):  # wait for complete lines
            chunk = chunk[: chunk.rfind("\n") + 1] if "\n" in chunk else ""
        self.offset += len(chunk)
        return [ln.strip() for ln in chunk.splitlines() if ln.strip()]


def open_capture(name: str) -> pathlib.Path:
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = pathlib.Path("captures") / f"{ts}-{name}.log"
    path.parent.mkdir(exist_ok=True)
    return path


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--name", default="session")
    ap.add_argument("--recipe", choices=sorted(RECIPES))
    ap.add_argument("--send", help="send one command, print replies for 3 s, exit")
    ap.add_argument("--ambient", type=float, help="ambient temp C (skips the prompt)")
    ap.add_argument("--duration", type=float, help="auto-stop after N seconds")
    ap.add_argument("--ctl", help="control file to poll (default <capture>.ctl)")
    ap.add_argument("--status-json", help="write a status summary here on exit")
    ap.add_argument("--replay", help="read from an existing log instead of serial")
    args = ap.parse_args()

    if not args.port and not args.replay:
        ap.error("--port is required (or --replay for dry runs)")
    src = ReplaySource(args.replay) if args.replay else SerialSource(args.port, args.baud)

    if args.send:
        src.write(args.send)
        end = time.monotonic() + 3.0
        while time.monotonic() < end:
            line = src.readline()
            if line:
                print(line)
        src.close()
        return

    ambient = args.ambient
    if ambient is None:
        if sys.stdin.isatty():
            ambient = float(input("ambient temperature [C]: ").strip() or "20")
        else:
            ambient = 20.0
            print("no --ambient given, assuming 20.0 C", file=sys.stderr)

    path = open_capture(args.name)
    out = open(path, "w")
    # Replayed logs carry their own #AMBIENT; don't write a conflicting one.
    if not args.replay:
        out.write(f"#AMBIENT {ambient:.1f}\n")
    print(f"recording -> {path}" +
          (f"  (auto-stop after {args.duration:.0f}s)" if args.duration else
           "  (Ctrl-C to stop)"))

    ctl = CtlFile(pathlib.Path(args.ctl) if args.ctl else path.with_suffix(".ctl"))
    recipe = list(RECIPES[args.recipe]) if args.recipe else []
    started = time.monotonic()
    next_step_at = started + (recipe[0][0] if recipe else 0)
    next_ctl_poll = started
    interactive = sys.stdin.isatty()
    rows = 0
    last_state = ""
    fault_seen = False
    stop_reason = "user"

    def send(cmd: str) -> None:
        print(f">> {cmd}")
        src.write(cmd)

    try:
        while True:
            now = time.monotonic()
            if args.duration and now - started >= args.duration:
                stop_reason = "duration"
                break
            if args.replay and src.exhausted:
                stop_reason = "replay-exhausted"
                break

            # device -> file (+echo)
            line = src.readline()
            if line:
                print(line)
                out.write(line + "\n")
                out.flush()
                if line[0].isdigit():
                    rows += 1
                    parts = line.split(",")
                    if len(parts) >= 12:
                        last_state = parts[10]
                        if last_state == "FAULT":
                            fault_seen = True

            # recipe steps
            if recipe and now >= next_step_at:
                _, cmd = recipe.pop(0)
                if cmd:
                    send(cmd)
                if recipe:
                    next_step_at = now + recipe[0][0]
                else:
                    print("recipe finished — waiting for tail/stop")

            # control-file commands (the wizard's channel)
            if now >= next_ctl_poll:
                next_ctl_poll = now + 0.5
                stop = False
                for cmd in ctl.poll():
                    if cmd == "STOP":
                        stop = True
                    else:
                        send(cmd)
                if stop:
                    # drain the device echo (e.g. a just-sent mark), bounded
                    for _ in range(20):
                        tail_line = src.readline()
                        if not tail_line:
                            break
                        out.write(tail_line + "\n")
                    stop_reason = "ctl-stop"
                    break

            # keyboard -> device (interactive sessions only)
            if interactive and select.select([sys.stdin], [], [], 0)[0]:
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
        stop_reason = "user"
        print()
    finally:
        out.close()
        src.close()
        seconds = time.monotonic() - started
        print(f"stopped ({stop_reason}) — capture in {path}")
        print(f"next: python tools/fit_model.py {path}")
        if args.status_json:
            with open(args.status_json, "w") as f:
                json.dump({"file": str(path), "rows": rows,
                           "seconds": round(seconds, 1),
                           "last_state": last_state,
                           "fault_seen": fault_seen,
                           "stop_reason": stop_reason}, f, indent=2)


if __name__ == "__main__":
    main()
