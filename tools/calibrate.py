#!/usr/bin/env python3
"""Fit a custom Steinhart-Hart curve per NTC from a sensor-observer capture
(apps/observer, env esp32c3-observer). Stdlib only.

Procedure (docs/tuning-hardware.md, step 0): before the retrofit, clamp one
DS18B20 together with each NTC, flash the observer firmware, start the
machine cold and let the stock bimetal cycle for ~45 min while recording:

    pio run -e esp32c3-observer -t upload
    python tools/tune_capture.py --port /dev/ttyACM0 --name sensor_cal
    python tools/calibrate.py captures/<date>-sensor_cal.log

The tool pairs each NTC millivolt reading with its co-located reference,
keeps only quasi-static samples (bimetal dwells / cold tail), and fits
1/T = A + B*lnR + C*ln^3 R per channel by linear least squares. Output is a
ready-to-paste NtcConfig block for src/config.h.

Caveats enforced/reported:
- clone gate: both refs must agree at cold start (same ambient) within 0.5 C
- DS18B20 accuracy is specified only up to 85 C; samples above are used but
  residuals are reported separately — anchor the top end with the boiling
  point check from docs/hardware.md if it matters
- `--selftest` synthesizes a run from known curves and asserts recovery
  (used in CI; no hardware needed).
"""

import argparse
import json
import math
import random
import sys

R_SERIES = 10000.0
V_SUPPLY_MV = 3300.0
K0 = 273.15


def parse(fp):
    rows = []
    header = None
    for raw in fp:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("millis"):
            header = line.split(",")
            continue
        if header is None or not line[0].isdigit():
            continue
        vals = line.split(",")
        if len(vals) != len(header):
            continue
        try:
            row = {k: float(v) for k, v in zip(header, vals)}
        except ValueError:
            continue
        row["t"] = row["millis"] / 1000.0
        rows.append(row)
    need = {"ntc_boiler_mv", "ntc_group_mv", "ref_boiler_C", "ref_group_C"}
    if rows and not need.issubset(rows[0]):
        raise SystemExit("not an observer capture (missing mv/ref columns)")
    return rows


def mean(xs):
    return sum(xs) / len(xs) if xs else float("nan")


def clone_gate(rows):
    """At cold start both probes sit at ambient: they must agree."""
    cold = [r for r in rows[:200] if not math.isnan(r["ref_boiler_C"])
            and not math.isnan(r["ref_group_C"])]
    cold = [r for r in cold if r["t"] - rows[0]["t"] < 120.0]
    if len(cold) < 10:
        return None  # not enough cold data to judge; report as unknown
    d = mean([r["ref_boiler_C"] - r["ref_group_C"] for r in cold])
    return d


def quasi_static(rows, ref_key, max_slope=0.02):
    """Samples where the local reference slope is below max_slope [K/s]."""
    out = []
    for i in range(2, len(rows) - 2):
        r = rows[i]
        if math.isnan(r[ref_key]):
            continue
        a, b = rows[i - 2], rows[i + 2]
        if math.isnan(a[ref_key]) or math.isnan(b[ref_key]):
            continue
        dt = b["t"] - a["t"]
        if dt <= 0:
            continue
        slope = (b[ref_key] - a[ref_key]) / dt
        if abs(slope) <= max_slope:
            out.append(r)
    return out


def fit_channel(samples, mv_key, ref_key):
    """LS fit of 1/T = A + B*x + C*x^3, x = ln(R).

    Samples are first averaged into 0.5 C reference bins: within a dwell the
    ADC noise averages out, so each bin becomes one low-noise (mv, T) point
    and the fit is driven by the temperature SPAN, not by whichever dwell
    lasted longest."""
    bins = {}
    for r in samples:
        mv = r[mv_key]
        if mv < 50.0 or mv > 3000.0:
            continue
        bins.setdefault(round(r[ref_key] * 2.0), []).append((mv, r[ref_key]))
    xs, ys, temps = [], [], []
    for pts in bins.values():
        mv = mean([p[0] for p in pts])
        tc = mean([p[1] for p in pts])
        rr = R_SERIES * (V_SUPPLY_MV - mv) / mv
        if rr <= 0:
            continue
        xs.append(math.log(rr))
        ys.append(1.0 / (tc + K0))
        temps.append(tc)
    n = len(xs)
    if n < 12:
        raise SystemExit(
            f"only {n} temperature bins for {mv_key} — record a longer run "
            "(cold tail, one mid-warm-up dwell, bimetal cycling)")

    # normal equations for [1, x, x^3]
    m = [[0.0] * 3 for _ in range(3)]
    v = [0.0] * 3
    for x, y in zip(xs, ys):
        b = (1.0, x, x ** 3)
        for i in range(3):
            for j in range(3):
                m[i][j] += b[i] * b[j]
            v[i] += b[i] * y

    # solve 3x3 (Cramer)
    def det3(a):
        return (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
                - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
                + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))

    d = det3(m)
    if abs(d) < 1e-30:
        raise SystemExit(f"degenerate fit for {mv_key} (temperature span too small)")
    coef = []
    for k in range(3):
        mk = [row[:] for row in m]
        for i in range(3):
            mk[i][k] = v[i]
        coef.append(det3(mk) / d)
    A, B, C = coef

    # residuals in temperature terms, split by trust region
    res_in, res_out = [], []
    for x, y, tc in zip(xs, ys, temps):
        inv_t = A + B * x + C * x ** 3
        err = (1.0 / inv_t - K0) - tc
        (res_in if tc <= 85.0 else res_out).append(err)

    def stats(res):
        if not res:
            return None
        return {"rms_C": round(math.sqrt(mean([e * e for e in res])), 3),
                "max_C": round(max(abs(e) for e in res), 3),
                "n": len(res)}

    # simple Beta+offset equivalent (2-param fit) for comparison
    sx = sum(xs); sy = sum(ys); sxx = sum(x * x for x in xs)
    sxy = sum(x * y for x, y in zip(xs, ys))
    db = n * sxx - sx * sx
    b2 = (n * sxy - sx * sy) / db
    a2 = (sy - b2 * sx) / n
    beta_eq = 1.0 / b2
    r25_eq = math.exp((1.0 / (25.0 + K0) - a2) * beta_eq)

    return {"shA": A, "shB": B, "shC": C,
            "span_C": [round(min(temps), 1), round(max(temps), 1)],
            "residuals_le85C": stats(res_in),
            "residuals_gt85C": stats(res_out),
            "beta_equivalent": round(beta_eq, 1),
            "r25_equivalent": round(r25_eq, 1),
            "samples": n}


def report(name, f):
    print(f"== {name} NTC ==")
    print(f"  span {f['span_C'][0]}..{f['span_C'][1]} C, {f['samples']} samples")
    print(f"  S-H: A={f['shA']:.6e}  B={f['shB']:.6e}  C={f['shC']:.6e}")
    print(f"  residuals <=85C: {f['residuals_le85C']}")
    print(f"  residuals  >85C: {f['residuals_gt85C']} (DS18B20 spec degrades; "
          "anchor with boiling point if this matters)")
    print(f"  Beta-equivalent: beta={f['beta_equivalent']} r25={f['r25_equivalent']}")
    print("  paste into src/config.h:")
    print(f"    cfg.useSteinhartHart = true;")
    print(f"    cfg.shA = {f['shA']:.6e}f;")
    print(f"    cfg.shB = {f['shB']:.6e}f;")
    print(f"    cfg.shC = {f['shC']:.6e}f;")


def run(fp, as_json):
    rows = parse(fp)
    if len(rows) < 100:
        raise SystemExit(f"only {len(rows)} rows — record a full bimetal run")

    gate = clone_gate(rows)
    if gate is None:
        print("WARNING: no cold-start window found; clone cross-check skipped",
              file=sys.stderr)
    elif abs(gate) > 0.5:
        raise SystemExit(
            f"reference probes disagree by {gate:+.2f} C at cold start — "
            "likely a counterfeit DS18B20; do not calibrate against these")

    out = {}
    for name, mv_key, ref_key in (("boiler", "ntc_boiler_mv", "ref_boiler_C"),
                                  ("group", "ntc_group_mv", "ref_group_C")):
        samples = quasi_static(rows, ref_key)
        out[name] = fit_channel(samples, mv_key, ref_key)

    if as_json:
        print(json.dumps(out, indent=2))
    else:
        if gate is not None:
            print(f"clone gate: refs agree within {gate:+.2f} C at cold start")
        for name in ("boiler", "group"):
            report(name, out[name])
    return out


# ---------------------------------------------------------------------------
# Self-test: synthesize an observer capture from known truth curves and
# assert the fit recovers them. Wired into CI.
# ---------------------------------------------------------------------------

def _truth_curve(beta, r25, dc):
    """Returns invT(lnR) coefficients of a Beta curve perturbed by a cubic
    term dc — a 'real' NTC that a pure Beta model cannot fit exactly."""
    A = 1.0 / (25.0 + K0) - math.log(r25) / beta
    return A, 1.0 / beta, dc


def _mv_from_temp(tc, A, B, C):
    # invert invT = A + B*lnR + C*ln^3R by bisection
    target = 1.0 / (tc + K0)
    lo, hi = math.log(100.0), math.log(10e6)
    for _ in range(80):
        mid = 0.5 * (lo + hi)
        if A + B * mid + C * mid ** 3 < target:
            lo = mid
        else:
            hi = mid
    r = math.exp(0.5 * (lo + hi))
    return V_SUPPLY_MV * R_SERIES / (r + R_SERIES)


def selftest():
    rng = random.Random(42)
    truth = {"boiler": _truth_curve(3921.0, 98700.0, 6e-8),
             "group": _truth_curve(3978.0, 101500.0, -4e-8)}

    # bimetal-style profile: cold dwell, ramp, cycling dwells
    lines = ["millis,ntc_boiler_mv,ntc_group_mv,ref_boiler_C,ref_group_C,"
             "ntc_boiler_C,ntc_group_C"]
    t = 0.0
    temp = 20.0
    while t < 3000.0:
        if t < 180.0:
            temp = 20.0                      # cold dwell (clone gate window)
        elif t < 420.0:
            temp = 20.0 + (t - 180.0) / 240.0 * 35.0   # ramp to 55
        elif t < 720.0:
            # mid-band mini-cycles (user power-cycling around ~55 C); the
            # quasi-static filter keeps the turning points
            temp = 55.0 + 0.8 * math.sin((t - 420.0) * 2.0 * math.pi / 180.0)
        elif t < 960.0:
            temp = 55.0 + (t - 720.0) / 240.0 * 42.0   # ramp to 97
        else:
            phase = ((t - 960.0) % 240.0) / 240.0      # bimetal triangle 93..99
            tri = 2.0 * abs(phase - 0.5)
            temp = 99.0 - 6.0 * tri
        vals = [f"{int(t * 1000)}"]
        mvs = []
        for ch in ("boiler", "group"):
            A, B, C = truth[ch]
            mv = _mv_from_temp(temp, A, B, C) + rng.uniform(-2.0, 2.0)
            mvs.append(f"{mv:.1f}")
        refs = []
        for _ in range(2):
            q = round((temp + rng.uniform(-0.05, 0.05)) / 0.0625) * 0.0625
            refs.append(f"{q:.3f}")
        lines.append(",".join(vals + mvs + refs + ["0", "0"]))
        t += 0.8

    import io
    out = run(io.StringIO("\n".join(lines)), as_json=False)

    failures = []
    for ch in ("boiler", "group"):
        A, B, C = truth[ch]
        f = out[ch]
        # functional accuracy: fitted curve vs truth across the band
        worst = 0.0
        for tc in range(25, 100, 5):
            mv = _mv_from_temp(tc, A, B, C)
            r = R_SERIES * (V_SUPPLY_MV - mv) / mv
            x = math.log(r)
            fitted = 1.0 / (f["shA"] + f["shB"] * x + f["shC"] * x ** 3) - K0
            worst = max(worst, abs(fitted - tc))
        if worst > 0.15:
            failures.append(f"{ch}: worst-case curve error {worst:.3f} C > 0.15")
        if f["residuals_le85C"]["rms_C"] > 0.1:
            failures.append(f"{ch}: residual RMS {f['residuals_le85C']['rms_C']}")
    if failures:
        print("SELFTEST FAILURES:", *failures, sep="\n  ", file=sys.stderr)
        sys.exit(1)
    print("selftest: fitted curves within 0.15 C of truth", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture", nargs="?", help="observer capture, or - for stdin")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--selftest", action="store_true",
                    help="synthesize a run from known curves, assert recovery")
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return
    if not args.capture:
        ap.error("capture file required (or --selftest)")
    fp = sys.stdin if args.capture == "-" else open(args.capture)
    run(fp, args.json)


if __name__ == "__main__":
    main()
