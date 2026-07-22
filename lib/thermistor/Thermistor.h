#pragma once

#include <cstdint>

namespace anita {

// NTC 3950, 100 kOhm @ 25 C, wired HIGH-SIDE:
//
//   3V3 ---[ NTC ]---+---[ Rs 10k ]--- GND
//                    |
//                   ADC
//
// Node voltage rises with temperature. Open NTC reads ~0 mV (node pulled to
// GND through Rs), shorted NTC reads ~Vsupply — both are detectable rail
// faults well outside the plausible temperature band.
struct NtcConfig {
    float beta = 3950.0f;        // K
    float r25 = 100000.0f;       // Ohm @ 25 C
    float rSeries = 10000.0f;    // Ohm, node -> GND
    float vSupplyMv = 3300.0f;   // divider supply
    float offsetC = 0.0f;        // one-point calibration trim

    // Per-sensor Steinhart-Hart curve (1/T = A + B*lnR + C*ln^3 R), fitted
    // from a DS18B20 reference run with tools/calibrate.py (see
    // docs/tuning-hardware.md step 0). When enabled it replaces the Beta
    // equation; offsetC still applies on top.
    bool useSteinhartHart = false;
    float shA = 0.0f;
    float shB = 0.0f;
    float shC = 0.0f;
};

enum class RailFault : uint8_t { None, Open, Short };

class Thermistor {
public:
    explicit Thermistor(const NtcConfig& cfg = {});

    float celsiusFromMillivolts(float mv) const;
    RailFault railFault(float mv) const;

    // Inverse conversion (temperature -> node mV); used by the simulator to
    // synthesize realistic ADC readings for the safety rail checks.
    float millivoltsFromCelsius(float tempC) const;

    static constexpr float kRailLowMv = 50.0f;
    static constexpr float kRailHighMv = 3000.0f;

private:
    NtcConfig cfg_;
};

}  // namespace anita
