---
name: tune-wizard
description: Guided hardware-tuning wizard for the Anita ADRC controller - walks the user through all capture scenarios at the machine (cold start, ident, setpoint step, espresso, flush, big draw), runs captures as background tasks, injects marks via the control file, validates every capture, then runs the retune and pauses before flashing.
---

# Tuning wizard

You are the conductor of a hardware tuning session. The user is physically at
the espresso machine; you run all tools, they only act on the machine when
you tell them to. Be concrete in physical instructions ("lock in the
portafilter with a blind basket") and patient — scenarios take many minutes.

## Ground rules

- **One capture at a time** — there is one serial port.
- Background captures always get `--duration` and `--status-json`; never rely
  on Ctrl-C.
- Inject commands into a *running* capture ONLY by appending lines to its
  `--ctl` file (`echo "mark espresso" >> <ctl>`). Never open the port
  yourself while a capture runs.
- Validate every capture with `fit_model.py --validate <scenario>` before
  marking it done. On failure: show the reasons, offer redo or skip
  (AskUserQuestion).
- If any status JSON shows `fault_seen: true`: stop everything, show the
  fault from the capture tail, tell the user to power-cycle the machine, and
  ask before continuing.
- Long waits: start the capture in the background, tell the user what will
  happen and when you'll need them again, and check the background task
  instead of blocking.
- **Never flash the firmware yourself.** The session ends with a diff and the
  flash command for the user.

## Preflight

1. Port: `ls /dev/ttyACM* /dev/ttyUSB*`. Multiple/none → ask.
2. `python3 -c "import serial"` — if missing, `pip install pyserial` (or use
   the project venv).
3. Read `captures/session.json` if it exists (manifest:
   `{scenario: {file, ok, ts, metrics}}`). Show a checklist table of done /
   pending scenarios and ask whether to resume or start fresh.
4. Ask for the ambient room temperature once; pass `--ambient` to every
   capture.
5. Dry-run mode: if the user has no hardware attached, substitute
   `--replay <sim log>` for `--port …` (generate one with
   `.pio/build/native/program --scenario <s> --serial-format > x.log`).

## Scenario suite

Work through these in order; each is skippable. `<cap>` =
`python3 tools/tune_capture.py --port <port> --ambient <a> --status-json captures/<scenario>.status.json`.
After each capture: validate, show the metrics vs targets, update
`captures/session.json` (write the whole manifest with the Write tool).

| # | Scenario | Ask first | Run |
|---|----------|-----------|-----|
| 1 | `first_heatup` (optional; only if this firmware never heated this machine) | machine cold? user ready to pull the plug if needed? | `<cap> --name first_heatup --duration 1500`; immediately inject `set cap 0.3`; watch status; after validation inject nothing — remind user cap resets on reboot |
| 2 | `cold_start` | machine off ≥3 h (fully cold)? | `<cap> --name cold_start --duration 2700`, passive; tell the user to just switch the machine on when you say go |
| 3 | `ident` | machine on ≥20 min, idle, no draws planned? | `<cap> --name ident --recipe id --duration 1500`; fully automatic |
| 4 | `setpoint_step` | warm and idle? | `<cap> --name setpoint_step --duration 1100`; inject `set setpoint 90`, wait ~8 min, inject `set setpoint 93`; restore original setpoint at the end |
| 5 | `espresso` | portafilter locked, cup in place? | `<cap> --name espresso --duration 420`; wait ~60 s steady, inject `mark espresso`, tell the user "pull a single shot (~30 ml/30 s) NOW"; tail runs out |
| 6 | `flush` | cup under the group? | `<cap> --name flush --duration 420`; inject `mark flush`, "open the brew valve for ~60 ml, then close" |
| 7 | `bigdraw` | large vessel (≥250 ml) ready? water tank full? | `<cap> --name bigdraw --duration 900`; inject `mark bigdraw`, "draw ~250 ml over ~30 s"; remind to refill the tank after |

Timing of marks matters more than exact volumes — inject the mark right when
the user says they are starting, not before asking.

## Gate

The final retune needs a **valid `ident`** (mandatory) and `cold_start`
(strongly recommended — warn explicitly if skipped). Scenarios 4–7 are for
closed-loop verification; missing ones just shrink the report.

## Retune (automatic once the gate passes)

Execute the `/retune` skill procedure (`.claude/skills/retune/SKILL.md`) on
the session's ident capture. Then extend its tuning-log entry with a
closed-loop metrics table from the validation verdicts of scenarios 4–7
against the targets in docs/adrc.md (overshoot <1 °C, espresso recovery
<60 s, bigdraw duty saturation <15 s, etc.).

## Pause before flash

Show: the git diff of model/param changes, the test results, and the metrics
table. Then hand over:

    pio run -e esp32c3 -t upload

Offer a post-flash verification lap (repeat scenario 4, compare overshoot and
settle time against the pre-flash numbers) and a final `git commit` of the
tuning-log entry + constants once the user is satisfied.
