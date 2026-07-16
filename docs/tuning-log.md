# Tuning log

Record every tuning run — simulator and hardware — so regressions are
traceable. Attach the plot PNG (`tools/plot_sim.py run.csv --save run.png`).

Template:

```
## YYYY-MM-DD — <sim|hardware> — <scenario / what changed>

Params: b0=…  ωc=…  ωo=…  predS=…  k_boost=…  offset_ss=…
Result: overshoot=… °C, steady band=±… °C, draw recovery=… s
Plot:   <link>
Verdict / next step:
```

---

## 2026-07-16 — sim — baseline defaults locked

Params: b0=0.55, ωc=0.04, ωo=0.16, Ts=0.5 s, predS=20, k_boost=2.0,
offset_init=6, ceiling=101 °C

| Scenario | Result |
|---|---|
| cold start (boost) | group ready in ~374 s, overshoot 0.33 °C, steady ±0.02 °C |
| cold start (no boost) | ready in ~410 s → boost saves ~36 s |
| espresso 30 ml/30 s | group dip 0.06 °C, instant recovery |
| max draw 250 ml/30 s | duty saturated 10.6 s after start, detected, ±1 °C in 110 s |
| warm-up flush 60 ml/15 s | no false triggers, boost decays, peak brass 105.0 °C |

Verdict: defaults committed; asserted in `test/test_controller_core`.
Next: hardware bring-up, refit BoilerModel constants from real heat-up slope
(C from slope, k_amb from steady-state duty).
