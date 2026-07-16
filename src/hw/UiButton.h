#pragma once

#include <Arduino.h>
#include <functional>

// Single-button UI on the boot button (GPIO9, active low):
//   short press  (< BTN_LONG_MS)  -> onShort  (+0.5 C)
//   long press   (>= BTN_LONG_MS) -> onLong   (-0.5 C), fires once while held
// Poll update() every few ms from the comms/UI task.
class UiButton {
public:
    std::function<void()> onShort;
    std::function<void()> onLong;

    void begin(int pin);
    void update();

private:
    int pin_ = -1;
    bool stable_ = false;      // debounced pressed state
    bool lastRaw_ = false;
    bool longFired_ = false;
    uint32_t lastEdgeMs_ = 0;
    uint32_t pressedAtMs_ = 0;
};
