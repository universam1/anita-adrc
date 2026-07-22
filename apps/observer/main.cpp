// Sensor observer — standalone calibration firmware (env: esp32c3-observer).
//
// One-time reference run BEFORE the retrofit, while the stock bimetal
// thermostat still controls the machine: clamp one DS18B20 together with
// each NTC (boiler pair on GPIO3's sensor, group pair on GPIO4's), wire the
// 1-Wire bus to GPIO7 with a 4.7k pullup to 3V3, flash this, start the
// machine cold and let the bimetal cycle for ~45 minutes while recording the
// serial output. Then: python tools/calibrate.py <capture> to fit a custom
// Steinhart-Hart curve per NTC. See docs/tuning-hardware.md, step 0.
//
// Streams one CSV row per DS18B20 conversion cycle (~800 ms — both sensors
// convert in parallel via one convert-all; the DS is deliberately the rate
// limiter). NTC channels are logged as raw calibrated millivolts using the
// exact acquisition the control firmware uses (11 dB attenuation, 16x
// oversampling), so fitted curves transfer 1:1.

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>

#include "Thermistor.h"

namespace {

constexpr int PIN_NTC_BOILER = 3;  // ADC1_CH3, same as the control firmware
constexpr int PIN_NTC_GROUP = 4;   // ADC1_CH4
constexpr int PIN_ONEWIRE = 7;     // 4.7k pullup to 3V3 required
constexpr uint32_t CONVERSION_MS = 760;  // 12-bit conversion + margin

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature ds(&oneWire);
DeviceAddress addr[2];
int dsCount = 0;

anita::Thermistor ntcCurve;  // default curve, for live eyeballing only

float readMv(int pin) {
    uint32_t acc = 0;
    for (int i = 0; i < 16; ++i) acc += analogReadMilliVolts(pin);
    return static_cast<float>(acc) / 16.0f;
}

void printAddress(const DeviceAddress& a) {
    for (int i = 0; i < 8; ++i) Serial.printf("%02X", a[i]);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(2000);  // give the USB CDC console time to attach

    pinMode(PIN_NTC_BOILER, INPUT);
    pinMode(PIN_NTC_GROUP, INPUT);
    analogSetPinAttenuation(PIN_NTC_BOILER, ADC_11db);
    analogSetPinAttenuation(PIN_NTC_GROUP, ADC_11db);

    ds.begin();
    ds.setResolution(12);
    ds.setWaitForConversion(false);  // async: we pace the loop ourselves
    dsCount = min(static_cast<int>(ds.getDeviceCount()), 2);
    Serial.printf("#INFO observer build, %d DS18B20 found\n", dsCount);
    for (int i = 0; i < dsCount; ++i) {
        if (ds.getAddress(addr[i], i)) {
            Serial.printf("#INFO ref%d rom=", i + 1);
            printAddress(addr[i]);
            Serial.println(i == 0 ? " (pair with BOILER ntc)"
                                  : " (pair with GROUP ntc)");
        }
    }
    if (dsCount < 2) {
        Serial.println("#INFO WARNING: expected 2 DS18B20 (boiler+group pair)");
    }
    Serial.println(
        "millis,ntc_boiler_mv,ntc_group_mv,ref_boiler_C,ref_group_C,"
        "ntc_boiler_C,ntc_group_C");
    ds.requestTemperatures();  // kick off the first convert-all
}

void loop() {
    delay(CONVERSION_MS);

    float ref[2] = {NAN, NAN};
    for (int i = 0; i < dsCount; ++i) {
        const float t = ds.getTempC(addr[i]);
        if (t > DEVICE_DISCONNECTED_C + 1.0f) ref[i] = t;
    }
    ds.requestTemperatures();  // both sensors convert in parallel again

    const float mvB = readMv(PIN_NTC_BOILER);
    const float mvG = readMv(PIN_NTC_GROUP);
    Serial.printf("%lu,%.1f,%.1f,%.3f,%.3f,%.2f,%.2f\n",
                  static_cast<unsigned long>(millis()), mvB, mvG, ref[0],
                  ref[1], ntcCurve.celsiusFromMillivolts(mvB),
                  ntcCurve.celsiusFromMillivolts(mvG));
}
