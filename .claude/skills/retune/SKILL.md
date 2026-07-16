---
name: retune
description: Update the boiler model and controller defaults from a hardware tuning capture (captures/*.log), re-verify sim + tests, log the change. Use after recording an identification run per docs/tuning-hardware.md.
---

# Retune from a hardware capture

Update this repo's plant-model assumptions and controller defaults from a
recorded tuning capture. Follow the steps in order; stop and report if a step
fails in a way the instructions don't cover.

## Inputs

The user (or the `/tune-wizard` skill, which invokes this procedure as its
final step) names a capture file; otherwise use the newest `captures/*.log`.
If there are no captures, stop and point at `docs/tuning-hardware.md` step 4.

## Steps

1. **Fit**: run `python3 tools/fit_model.py <capture> --json` and also the
   human report (without `--json`). Sanity-check: `C_from_cooling` and
   `C_from_step` should agree within ~25 %; the `b0_from_z2` cross-check is
   usually more trustworthy than `b0 = P/C`. If the fits are wildly
   inconsistent, report that and stop — a bad capture must not update the
   model.

2. **Update the model** (`lib/plant_model/BoilerModel.h` defaults):
   - Scale `cWater`/`cBrass` so their sum equals fitted `C_total`
     (keep the water share at 0.25 l × 4186 = 1046 J/K fixed; brass absorbs
     the remainder).
   - `kAmbB = k_lumped − k_ambG`, `kBg = k_bg`, `kAmbG = k_ambG`.
   - Set `tauNtcBoilerS + delayBoilerS` consistent with the fitted `lag`
     (keep the 8:2 ratio unless the capture suggests otherwise).

3. **Update the controller defaults** (`lib/adrc/Adrc.h` AdrcParams):
   - `b0` from the z2 cross-check (fallback: P/C_total).
   - Verify `wc ≤ 1/(2·lag)`; if the fitted lag is larger than assumed,
     reduce `wc` and keep `wo = 4·wc`, `predS ≈ 2·lag`.

4. **Re-verify**: `pio test -e native`. If the closed-loop contract
   (`test/test_controller_core`) fails, re-tune in sim per `docs/adrc.md`
   (sweep with `.pio/build/native/program --scenario … --wc … --wo … --pred …`)
   until the scenarios meet the plan targets again, then update BOTH the
   defaults and, only where physically justified, the test tolerances.
   Never loosen a tolerance without writing down why in the tuning log.

5. **Round-trip check**: `pio run -e native && .pio/build/native/program
   --scenario ident --serial-format | python3 tools/fit_model.py - --expect-sim`.
   If the fitted-from-sim constants now sit outside the `EXPECT_SIM` windows
   in `tools/fit_model.py`, recenter those windows on the new model truth.

6. **Log**: append a dated entry to `docs/tuning-log.md`: capture file,
   fitted values, params before → after, test results.

7. **Build**: `pio run -e esp32c3`. Summarize the diff for the user and tell
   them it is ready to flash (`pio run -e esp32c3 -t upload`). Do not flash
   yourself.
