#!/usr/bin/env python3
"""Fit boiler-model constants from a tuning capture (device serial stream or
`sim --scenario ident --serial-format` output). Stdlib only — runs in CI.

The capture must contain a steady regulation window followed by the
identification recipe (see docs/tuning-hardware.md):

    steady -> id off 420 -> id duty 0.4 120 -> id off 180

Fits (energy-balance based, no optimizer):
    k_lumped   total loss [W/K]         P*duty_ss / (T_ss - T_amb)
    C_total    heat capacity [J/K]      from segment slopes: P*d = C*dT/dt + k*(T-Ta)
    b0         ADRC gain [K/s]          P / C_total, cross-checked vs -z2_ss/duty_ss
    lag        effective sensor lag [s] time to half of max slope after the duty step
    k_bg, k_ambG  group couplings       least squares on dTg/dt = a*(Tb-Tg) + b*(Tg-Ta)
                                        (Cg assumed, --cg to override)

Usage:
    python tools/fit_model.py captures/2026-.. .log
    .pio/build/native/program --scenario ident --serial-format | \\
        python tools/fit_model.py - --expect-sim
"""

import argparse
import json
import math
import sys

P_HEATER = 1000.0  # W

# Current committed defaults (BoilerModelParams / AdrcParams) for comparison.
# "lag" is the COMPOSITE reaction lag of the whole chain (heater -> brass ->
# sensor -> filtering): the controller-relevant number for the wc bound. The
# model's sensor stage alone is tauNtc=8s + delay=2s, but the brass initially
# heats fast (P/C_brass) before energy couples into the water, so the chain's
# reaction lag is ~4-5s in the simulator.
DEFAULTS = {
    "C_total": 760.0 + 1046.0,
    "k_lumped": 1.2 + 0.5,  # kAmbB + kAmbG (lumped view)
    "b0": 0.55,
    "lag": 4.0,             # composite reaction lag of the current model
    "k_bg": 6.0,
    "k_ambG": 0.5,
}

# Tolerance windows for --expect-sim (round-trip validation against the
# simulator's known truth; generous because the fits are deliberately simple).
EXPECT_SIM = {
    "C_total": (1300.0, 2400.0),        # truth 1806
    "k_lumped": (1.1, 2.3),             # truth ~1.6 at 99 C
    "b0": (0.40, 0.75),                 # truth 0.55
    "lag": (2.5, 6.5),                  # composite reaction lag, sim ~4.0
    "lag_dead_time_s": (0.0, 2.5),      # sim ~0.5 (2 s transport, partly
                                        # masked by bucketed slopes)
    "lag_tau_s": (1.5, 6.0),            # sim ~3.5
    "k_bg": (3.5, 9.0),                 # truth 6
    "k_ambG": (0.25, 1.0),              # truth 0.5
}


def parse_capture(fp):
    rows, events, meta = [], [], {"ambient": None, "params": {}, "marks": []}
    header = None
    for raw in fp:
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            parts = line.split()
            tag = parts[0]
            if tag == "#AMBIENT" and len(parts) > 1:
                meta["ambient"] = float(parts[1])
            elif tag == "#MARK" and len(parts) > 2:
                meta["marks"].append({"t": float(parts[1]) / 1000.0,
                                      "label": " ".join(parts[2:])})
            elif tag == "#EVT" and len(parts) > 2:
                # args live in their own dict — the duration arg is also
                # named "t" and must not clobber the event timestamp
                ev = {"t": float(parts[1]) / 1000.0, "name": parts[2], "args": {}}
                for kv in parts[3:]:
                    if "=" in kv:
                        k, v = kv.split("=", 1)
                        ev["args"][k] = float(v)
                events.append(ev)
            elif tag == "#PARAMS":
                for kv in parts[2:]:
                    if "=" in kv:
                        k, v = kv.split("=", 1)
                        meta["params"][k] = float(v)
            continue
        if line.startswith("millis"):
            header = line.split(",")
            continue
        if header is None or not line[0].isdigit():
            continue
        vals = line.split(",")
        if len(vals) != len(header):
            continue
        row = {}
        for k, v in zip(header, vals):
            if k in ("state",):
                row[k] = v
            elif k in ("draw",):
                row[k] = int(v)
            else:
                row[k] = float(v)
        row["t"] = row["millis"] / 1000.0
        rows.append(row)
    return rows, events, meta


def window(rows, t0, t1):
    return [r for r in rows if t0 <= r["t"] <= t1]


def mean(xs):
    return sum(xs) / len(xs) if xs else float("nan")


def linfit(ts, ys):
    """Least-squares slope and intercept of ys over ts."""
    n = len(ts)
    if n < 3:
        return float("nan"), float("nan")
    mt, my = mean(ts), mean(ys)
    sxx = sum((t - mt) ** 2 for t in ts)
    sxy = sum((t - mt) * (y - my) for t, y in zip(ts, ys))
    slope = sxy / sxx if sxx > 0 else float("nan")
    return slope, my - slope * mt


def segment_bounds(events, name, index=0):
    """Return (t_start, t_end, ev) of the index-th EVT matching `name`."""
    found = 0
    for i, ev in enumerate(events):
        if ev["name"] != name:
            continue
        if found == index:
            t_end = None
            for later in events[i + 1:]:
                if later["name"] in ("id_end", "id_stop"):
                    t_end = later["t"]
                    break
            return ev["t"], t_end, ev
        found += 1
    return None, None, None


def duty_segments_by_role(events):
    """The id recipe fires two duty steps with different jobs: the full-power
    pulse (d >= 0.9) is the sharp edge for the lag fit, the moderate one
    (d < 0.9) the energy balance for C. Old single-step captures fall back to
    using the one segment for both."""
    lag_seg, cstep_seg = None, None
    idx = 0
    while True:
        t0, t1, ev = segment_bounds(events, "id_duty", idx)
        if t0 is None:
            break
        d = ev["args"].get("d", float("nan"))
        if d >= 0.9 and lag_seg is None:
            lag_seg = (t0, t1, d)
        elif d < 0.9 and cstep_seg is None:
            cstep_seg = (t0, t1, d)
        idx += 1
    if cstep_seg is None:
        cstep_seg = lag_seg
    if lag_seg is None:
        lag_seg = cstep_seg
    return lag_seg, cstep_seg


def fit_lag(rows, t0, t1):
    """Dead time L and sensor time constant tau from the slope transition
    after an exactly-timestamped duty step at t0:

        s(t) = s_pre                                   t <= t0 + L
        s(t) = s_pre + ds * (1 - exp(-(t-t0-L)/tau))   t >  t0 + L

    Slopes come from central differences on 2 s buckets over [t0-30, t1];
    (L, tau) by grid search with (s_pre, ds) solved linearly at each node."""
    w = window(rows, t0 - 30.0, t1)
    bucket = {}
    for r in w:
        bucket.setdefault(int(r["t"] // 2), []).append(r["boiler"])
    keys = sorted(bucket.keys())
    pts = [(k * 2.0 + 1.0, mean(bucket[k])) for k in keys]
    slopes = []
    for i in range(1, len(pts) - 1):
        dt = pts[i + 1][0] - pts[i - 1][0]
        if dt > 0:
            slopes.append((pts[i][0], (pts[i + 1][1] - pts[i - 1][1]) / dt))
    if len(slopes) < 8:
        return float("nan"), float("nan")

    best = (float("inf"), float("nan"), float("nan"))
    lval = 0.0
    while lval <= 10.0:
        tau = 1.0
        while tau <= 40.0:
            # linear LS for (s_pre, ds) given basis f(t)
            s11 = s12 = s22 = b1 = b2 = 0.0
            for t, s in slopes:
                f = 0.0
                if t > t0 + lval:
                    f = 1.0 - math.exp(-(t - t0 - lval) / tau)
                s11 += 1.0
                s12 += f
                s22 += f * f
                b1 += s
                b2 += s * f
            det = s11 * s22 - s12 * s12
            if abs(det) > 1e-9:
                s_pre = (b1 * s22 - b2 * s12) / det
                ds = (b2 * s11 - b1 * s12) / det
                sse = 0.0
                for t, s in slopes:
                    f = 0.0
                    if t > t0 + lval:
                        f = 1.0 - math.exp(-(t - t0 - lval) / tau)
                    e = s - (s_pre + ds * f)
                    sse += e * e
                if ds > 0 and sse < best[0]:
                    best = (sse, lval, tau)
            tau += 0.25
        lval += 0.25
    return best[1], best[2]


def fit(rows, events, meta, cg):
    if not rows:
        raise SystemExit("no CSV rows found in capture")
    t_amb = meta["ambient"]
    if t_amb is None:
        raise SystemExit("no #AMBIENT line in capture (tune_capture.py writes it)")
    if not events:
        raise SystemExit("no #EVT lines — run the id recipe (tune_capture.py --recipe id)")

    out = {}

    # --- steady window: 120 s of regulation right before the first EVT
    t_first = events[0]["t"]
    steady = [r for r in window(rows, t_first - 120.0, t_first - 1.0)
              if r["state"] == "reg" and r["draw"] == 0]
    if len(steady) < 20:
        raise SystemExit("no steady regulation window before the first #EVT")
    duty_ss = mean([r["duty"] for r in steady])
    t_ss = mean([r["boiler"] for r in steady])
    tg_ss = mean([r["group"] for r in steady])
    z2_ss = mean([r["z2"] for r in steady])
    out["duty_ss"] = duty_ss
    out["T_ss"] = t_ss
    out["k_lumped"] = P_HEATER * duty_ss / (t_ss - t_amb)

    # --- cooling segment (first id_off): C from energy balance at zero power
    t0, t1, _ = segment_bounds(events, "id_off", 0)
    if t0 is None:
        raise SystemExit("no id_off segment in capture")
    w = window(rows, t0 + 30.0, min(t1 or t0 + 400.0, t0 + 300.0))
    slope_cool, _ = linfit([r["t"] for r in w], [r["boiler"] for r in w])
    t_mid = mean([r["boiler"] for r in w])
    c_cool = -out["k_lumped"] * (t_mid - t_amb) / slope_cool
    out["C_from_cooling"] = c_cool

    # --- duty step segments by role (lag pulse vs C step)
    lag_seg, cstep_seg = duty_segments_by_role(events)
    if cstep_seg is None:
        raise SystemExit("no id_duty segment in capture")

    t0, t1, d = cstep_seg
    w = window(rows, t0 + 30.0, t1 or t0 + 120.0)
    slope_step, _ = linfit([r["t"] for r in w], [r["boiler"] for r in w])
    t_mid = mean([r["boiler"] for r in w])
    c_step = (P_HEATER * d - out["k_lumped"] * (t_mid - t_amb)) / slope_step
    out["C_from_step"] = c_step

    out["C_total"] = mean([c for c in (c_cool, c_step) if c > 0])
    out["b0"] = P_HEATER / out["C_total"]
    out["b0_from_z2"] = -z2_ss / duty_ss if duty_ss > 0.01 else float("nan")

    # --- sensor lag: dead time + tau fitted to the slope transition after
    # the full-power pulse (the #EVT timestamp of the cause is exact)
    lt0, lt1, _ = lag_seg
    lag_l, lag_tau = fit_lag(rows, lt0, lt1 or lt0 + 30.0)
    out["lag_dead_time_s"] = lag_l
    out["lag_tau_s"] = lag_tau
    out["lag"] = lag_l + lag_tau

    # --- group couplings: LS on dTg/dt = a*(Tb-Tg) + b*(Tg-Ta), Cg assumed.
    # Resample to 10 s buckets to knock down noise and the group NTC lag.
    bucket, xs1, xs2, ys = {}, [], [], []
    for r in rows:
        if r["draw"]:
            continue
        bucket.setdefault(int(r["t"] // 10), []).append(r)
    keys = sorted(bucket.keys())
    series = [(k * 10.0 + 5.0,
               mean([r["boiler"] for r in bucket[k]]),
               mean([r["group"] for r in bucket[k]])) for k in keys]
    for i in range(1, len(series) - 1):
        t_prev, _, g_prev = series[i - 1]
        t_next, _, g_next = series[i + 1]
        _, b_i, g_i = series[i]
        dgdt = (g_next - g_prev) / (t_next - t_prev)
        xs1.append(b_i - g_i)
        xs2.append(g_i - t_amb)
        ys.append(dgdt)
    # normal equations for y = a*x1 + b*x2
    s11 = sum(x * x for x in xs1)
    s22 = sum(x * x for x in xs2)
    s12 = sum(x1 * x2 for x1, x2 in zip(xs1, xs2))
    sy1 = sum(y * x for y, x in zip(ys, xs1))
    sy2 = sum(y * x for y, x in zip(ys, xs2))
    det = s11 * s22 - s12 * s12
    if abs(det) > 1e-12:
        a = (sy1 * s22 - sy2 * s12) / det
        b = (sy2 * s11 - sy1 * s12) / det
        out["k_bg"] = a * cg
        out["k_ambG"] = -b * cg
    out["group_offset_ss"] = t_ss - tg_ss
    return out


def _find_mark(meta, label):
    for m in meta["marks"]:
        if label in m["label"]:
            return m
    return None


def validate(rows, events, meta, scenario, cg):
    """Per-scenario capture quality gate for the tuning wizard.

    Returns {"ok": bool, "scenario": str, "reasons": [..], "metrics": {..}}.
    The metrics double as the closed-loop verification report (targets in
    docs/adrc.md)."""
    reasons, metrics = [], {}
    scenario = "bigdraw" if scenario == "maxdraw" else scenario

    # common checks
    if meta["ambient"] is None:
        reasons.append("missing #AMBIENT line")
    if len(rows) < 100:
        reasons.append(f"only {len(rows)} CSV rows")
    else:
        if any(r["state"] == "FAULT" for r in rows):
            reasons.append("FAULT state present in capture")
        if not all(-5.0 < r["boiler"] < 150.0 for r in rows):
            reasons.append("implausible boiler temperatures")

    def done():
        return {"ok": not reasons, "scenario": scenario,
                "reasons": reasons, "metrics": metrics}

    if reasons:
        return done()

    setpoint = rows[-1]["set"]
    t_end = rows[-1]["t"]

    if scenario in ("cold_start", "first_heatup"):
        if rows[0]["boiler"] > 40.0:
            reasons.append(f"not a cold start (begins at {rows[0]['boiler']:.0f}C)")
        if not any(r["state"] == "reg" for r in rows):
            reasons.append("never reached regulation")
        t_ready = next((r["t"] for r in rows if r["group"] >= setpoint - 1.0), None)
        if t_ready is None:
            reasons.append("group never got within 1C of setpoint")
        else:
            metrics["time_to_ready_s"] = round(t_ready - rows[0]["t"], 1)
        metrics["group_overshoot_C"] = round(
            max(r["group"] for r in rows) - setpoint, 2)
        tail = [r for r in rows if r["t"] > t_end - 300.0]
        metrics["steady_band_C"] = round(
            max(abs(r["group"] - setpoint) for r in tail), 2)

    elif scenario == "ident":
        try:
            out = fit(rows, events, meta, cg)
        except SystemExit as e:
            reasons.append(str(e))
            return done()
        for key in ("C_total", "b0", "k_lumped", "lag"):
            v = out.get(key)
            if v is None or math.isnan(v):
                reasons.append(f"fit produced no {key}")
        cc, cs = out.get("C_from_cooling"), out.get("C_from_step")
        if cc and cs and (cc <= 0 or cs <= 0 or
                          abs(cc - cs) / max(cc, cs) > 0.35):
            reasons.append(
                f"C estimates disagree (cooling {cc:.0f} vs step {cs:.0f})")
        metrics.update({k: round(v, 3) for k, v in out.items()
                        if isinstance(v, float) and not math.isnan(v)})

    elif scenario == "setpoint_step":
        sets = [r["set"] for r in rows]
        if max(sets) - min(sets) < 2.0:
            reasons.append("setpoint never stepped by >=2C")
        else:
            # last upward change of the setpoint column
            t_step = next(
                (rows[i]["t"] for i in range(len(rows) - 1, 0, -1)
                 if rows[i]["set"] > rows[i - 1]["set"]), None)
            if t_step is None:
                reasons.append("no upward setpoint step found")
            else:
                after = [r for r in rows if r["t"] >= t_step]
                metrics["overshoot_C"] = round(
                    max(r["boiler"] - r["boiler_set"] for r in after), 2)
                out_of_band = [r["t"] for r in after
                               if abs(r["boiler"] - r["boiler_set"]) > 0.5]
                metrics["settle_s"] = round(
                    (out_of_band[-1] - t_step) if out_of_band else 0.0, 1)

    elif scenario in ("espresso", "flush", "bigdraw"):
        m = _find_mark(meta, scenario)
        if m is None:
            reasons.append(f"no #MARK containing '{scenario}'")
            return done()
        pre = [r for r in rows if m["t"] - 60.0 <= r["t"] < m["t"]]
        post = [r for r in rows if m["t"] <= r["t"] <= m["t"] + 300.0]
        if len(pre) < 10 or len(post) < 50:
            reasons.append("not enough data around the mark")
            return done()
        z2_before = mean([r["z2"] for r in pre])
        z2_min = min(r["z2"] for r in post)
        duty_max = max(r["duty"] for r in post)
        if z2_before - z2_min < 0.03 and duty_max < 0.5:
            reasons.append("no visible draw response after the mark")
        metrics["group_dip_C"] = round(
            setpoint - min(r["group"] for r in post), 2)
        band = 1.0 if scenario == "bigdraw" else 0.5
        out_of_band = [r["t"] for r in post
                       if abs(r["group"] - setpoint) > band]
        metrics[f"recovery_to_{band}C_s"] = round(
            (out_of_band[-1] - m["t"]) if out_of_band else 0.0, 1)
        metrics["draw_detected"] = any(r["draw"] for r in post)
        if scenario == "bigdraw":
            sat = next((r["t"] - m["t"] for r in post if r["duty"] >= 0.99), None)
            metrics["duty_saturation_latency_s"] = (
                round(sat, 1) if sat is not None else None)

    else:
        reasons.append(f"unknown scenario '{scenario}'")

    return done()


def report(out, meta):
    print("== fit_model report ==")
    if meta["params"]:
        print("device params:", " ".join(f"{k}={v:g}" for k, v in meta["params"].items()))
    print(f"steady: duty={out['duty_ss']:.3f} T={out['T_ss']:.1f}C "
          f"group offset={out['group_offset_ss']:.1f}C")
    print()
    print(f"{'quantity':<12} {'fitted':>10} {'current':>10}   note")
    rows = [
        ("k_lumped", out.get("k_lumped"), DEFAULTS["k_lumped"], "W/K total loss"),
        ("C_total", out.get("C_total"), DEFAULTS["C_total"],
         f"J/K (cooling {out.get('C_from_cooling', float('nan')):.0f} / "
         f"step {out.get('C_from_step', float('nan')):.0f})"),
        ("b0", out.get("b0"), DEFAULTS["b0"],
         f"K/s (z2 cross-check {out.get('b0_from_z2', float('nan')):.2f})"),
        ("lag", out.get("lag"), DEFAULTS["lag"],
         f"s reaction lag of the chain (dead {out.get('lag_dead_time_s', float('nan')):.1f}"
         f" + tau {out.get('lag_tau_s', float('nan')):.1f})"),
        ("k_bg", out.get("k_bg"), DEFAULTS["k_bg"], "W/K boiler->group (Cg assumed)"),
        ("k_ambG", out.get("k_ambG"), DEFAULTS["k_ambG"], "W/K group->ambient"),
    ]
    for name, fitted, cur, note in rows:
        f = "n/a" if fitted is None or math.isnan(fitted) else f"{fitted:10.2f}"
        print(f"{name:<12} {f:>10} {cur:>10.2f}   {note}")
    print()
    print("suggested: BoilerModelParams.kAmbB = k_lumped - k_ambG;"
          " cWater+cBrass = C_total; AdrcParams.b0 = b0;"
          " wc <= 1/(2*lag); predS ~= 4*lag;"
          " tune tauNtcBoilerS until the sim round-trip reproduces lag")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture", help="capture file, or - for stdin")
    ap.add_argument("--cg", type=float, default=500.0,
                    help="assumed group heat capacity J/K (default 500)")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--expect-sim", action="store_true",
                    help="assert fits recover the simulator's known constants")
    ap.add_argument("--validate", metavar="SCENARIO",
                    help="quality-gate the capture for a scenario "
                         "(cold_start ident setpoint_step espresso flush "
                         "bigdraw); prints JSON, exit 1 if not ok")
    args = ap.parse_args()

    fp = sys.stdin if args.capture == "-" else open(args.capture)
    rows, events, meta = parse_capture(fp)

    if args.validate:
        verdict = validate(rows, events, meta, args.validate, args.cg)
        print(json.dumps(verdict, indent=2))
        sys.exit(0 if verdict["ok"] else 1)

    out = fit(rows, events, meta, args.cg)

    if args.json:
        print(json.dumps(out, indent=2))
    else:
        report(out, meta)

    if args.expect_sim:
        failures = []
        for key, (lo, hi) in EXPECT_SIM.items():
            v = out.get(key)
            if v is None or math.isnan(v) or not (lo <= v <= hi):
                failures.append(f"{key}={v} not in [{lo}, {hi}]")
        if failures:
            print("EXPECT-SIM FAILURES:", *failures, sep="\n  ", file=sys.stderr)
            sys.exit(1)
        print("expect-sim: all fits within tolerance", file=sys.stderr)


if __name__ == "__main__":
    main()
