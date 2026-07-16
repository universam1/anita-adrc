#include "UiButton.h"

#include "config.h"

void UiButton::begin(int pin) {
    pin_ = pin;
    pinMode(pin_, INPUT_PULLUP);
}

void UiButton::update() {
    const uint32_t now = millis();
    const bool raw = digitalRead(pin_) == LOW;

    if (raw != lastRaw_) {
        lastRaw_ = raw;
        lastEdgeMs_ = now;
    }
    if (now - lastEdgeMs_ < BTN_DEBOUNCE_MS) return;

    if (raw && !stable_) {  // press
        stable_ = true;
        longFired_ = false;
        pressedAtMs_ = now;
    } else if (raw && stable_ && !longFired_ && now - pressedAtMs_ >= BTN_LONG_MS) {
        longFired_ = true;
        if (onLong) onLong();
    } else if (!raw && stable_) {  // release
        stable_ = false;
        if (!longFired_ && now - pressedAtMs_ < BTN_LONG_MS) {
            if (onShort) onShort();
        }
    }
}
