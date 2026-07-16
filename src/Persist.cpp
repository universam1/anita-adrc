#include "Persist.h"

#include "config.h"

void Persist::begin() { prefs_.begin("anita", false); }

float Persist::loadSetpoint(float fallback) {
    return prefs_.getFloat("set", fallback);
}

float Persist::loadOffset(float fallback) {
    return prefs_.getFloat("offset", fallback);
}

void Persist::noteSetpointChanged(float c) {
    pendingSetpoint_ = c;
    setpointDirty_ = true;
    setpointChangedMs_ = millis();
}

void Persist::update() {
    if (setpointDirty_ && millis() - setpointChangedMs_ >= NVS_QUIET_MS) {
        prefs_.putFloat("set", pendingSetpoint_);
        setpointDirty_ = false;
    }
}

void Persist::saveOffset(float c) { prefs_.putFloat("offset", c); }
