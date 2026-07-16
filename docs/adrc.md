# ADRC design and tuning

## Plant abstraction

The boiler is treated as a first-order system with an unknown lumped
disturbance:

```
dy/dt = f(t) + b0 · u
```

- `y` — boiler shell temperature (the shell-clamped NTC)
- `u` — heater duty ∈ [0, 1] (1000 W element via burst-fired SSR)
- `f(t)` — *total disturbance*: ambient loss, water draws, group coupling,
  model error, sensor-lag effects. Everything we did not model.

ADRC's bet: don't model `f`, **estimate and cancel it** every cycle.

## b0 from physics

```
b0 ≈ P / (C_water + C_brass) = 1000 W / (0.25·4186 + 2.0·380) J/K ≈ 0.55 K/s
```

ADRC tolerates roughly ±50 % error in `b0`; the observer absorbs the
mismatch into `z2`.

## Discrete LADRC (Ts = 0.5 s)

2nd-order linear extended state observer (LESO), forward Euler, driven by the
duty **actually delivered** by the modulator (`u_lim`):

```
e      = y[k] − z1[k]
z1[k+1] = z1[k] + Ts · ( z2[k] + b0·u_lim[k] + β1·e )      β1 = 2·ωo
z2[k+1] = z2[k] + Ts · ( β2·e )                            β2 = ωo²
```

Control law with lag compensation (`predS`, see below):

```
y_pred = z1 + predS · ( z2 + b0·u_lim )        # ESO gives dy/dt for free
u0     = ωc · ( r − y_pred )
u      = clamp( (u0 − z2) / b0 , 0, 1 )        # clamped u fed back to ESO
```

Feeding the *saturated* `u` into the ESO is the anti-windup: during a
full-duty heat-up the saturation mismatch lands honestly in `z2` instead of
winding up an integrator, so the handover to regulation is bumpless.

### Why the predS term

The shell NTC lags the brass by ~8 s plus ~2 s transport delay. During a
1 K/s heat-up ramp the sensed temperature is ~10 °C behind reality — a plain
P law on `z1` overshoots. `y_pred` extrapolates the ESO's own rate estimate
`dy/dt = z2 + b0·u` over `predS` seconds. At steady state `dy/dt ≈ 0`, so the
term adds **zero bias**; on a ramp it backs the duty off early. In the
simulator, `predS = 20 s` cuts the approach overshoot from ~2.5 °C to <1 °C.

## Default parameters (simulator-derived)

| Parameter | Value | Note |
|---|---|---|
| `b0` | 0.55 K/s | from physics above |
| `ωc` | 0.04 rad/s | ≈25 s closed-loop time constant |
| `ωo` | 0.16 rad/s | 4·ωc |
| `Ts` | 0.5 s | ADRC period; SSR modulation is independent at 10 ms |
| `predS` | 20 s | ≈ sensor lag + transport delay, ×2 |

Sensor lag bounds the achievable bandwidth (`ωc ≲ 1/(2·L)` with L ≈ 10–20 s);
pushing `ωc` past ~0.1 rad/s will oscillate on hardware no matter how good
the simulation looks.

## Cascade: group temperature is the target

The user setpoint (85–98 °C) refers to the **brew group**. `GroupComp`
elevates the boiler setpoint by the measured group deficit:

```
boiler_set = group_set + offset_ss + k_boost · (group_set − group_temp)
```

clamped to [`minBoost`, `maxBoost`] = [−5, +15] °C and an absolute ceiling of
101 °C (4 °C under the 105 °C software overtemp trip).

- The ΔT term is instantaneous feedforward — it cannot oscillate against the
  inner loop. It covers cold-start self-heating, warm-up flushes and
  cool-down automatically because the deficit is *measured* every cycle.
- `offset_ss` (steady-state boiler↔group offset) is integral-learned only at
  quiescence (boiler settled, no draw, boost ≈ 0) at 0.002 /s — two to three
  orders slower than the inner loop — and persisted to NVS.

## Draw detection

`z2` *is* a live disturbance wattmeter. A draw pulls cold water in and shows
up as a negative step in `z2` within seconds, before the lagged NTC moves.
`DrawDetector` triggers on `z2 < baseline − 0.05 K/s` (baseline = slow EMA,
frozen during draws), debounced 1 s, capped at 90 s with a return-to-baseline
lockout. While a draw is active the duty is fed forward to 100 % (unless the
boiler is already above target).

Detection is enabled only after regulation has first been reached — the
warm-up transient in `z2` would otherwise false-trigger.

## Tuning procedure (sim first, then hardware)

> Hardware tuning has its own end-to-end playbook — capture, model fitting
> and the `/retune` update loop: [tuning-hardware.md](tuning-hardware.md).

1. Fix `b0 = 0.55`. Step the setpoint 90→93 °C from steady state. Raise `ωc`
   until the first overshoot appears, back off 30 %.
2. Raise `ωo` (keep 3–5×ωc) until sensor noise visibly appears in the duty;
   back off.
3. Disturbance test: `startDraw(30, 30)` in sim / pull a blank shot on the
   machine. Target: recovery to ±0.5 °C at the group in <60 s.
4. Duty oscillates slowly around the setpoint → `b0` too small, increase.
   Sluggish response with a large drifting `z2` → `b0` too big.
5. `k_boost` (live-tunable over MQTT): higher = faster warm-up but more group
   overshoot when the boost decays. Default 2.0.
6. Record every run in [tuning-log.md](tuning-log.md) using
   `tools/plot_sim.py` / `tools/plot_serial.py`.

The simulator CLI accepts overrides for all of these:

```
.pio/build/native/program --scenario cold_start --wc 0.05 --wo 0.2 --pred 15 --csv run.csv
```

## Regression contract

`test/test_controller_core` asserts the tuned behavior in CI:

- cold start: group ready (<1 °C from set) in <600 s, overshoot <1 °C,
  steady band ±0.2 °C, boiler never above 103 °C sensed / 104 °C brass
- ΔT boost measurably faster than no-boost, at no overshoot cost
- single espresso (30 ml/30 s): group dip <1.5 °C, recovered <60 s
- max draw (250 ml/30 s ≈ full boiler volume): duty saturates <15 s,
  detected, recovered to ±1 °C <180 s, no windup, no fault
- warm-up flush: no false draw triggers, boost decays, no fault
