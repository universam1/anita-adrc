#pragma once

#include <Preferences.h>

// NVS persistence with wear protection: the setpoint is written only after
// NVS_QUIET_MS without further changes, the learned offset only in the coarse
// steps GroupComp flags as dirty.
class Persist {
public:
    void begin();

    float loadSetpoint(float fallback);
    float loadOffset(float fallback);

    void noteSetpointChanged(float c);  // starts/extends the quiet window
    void update();                      // call periodically; flushes when quiet
    void saveOffset(float c);

private:
    Preferences prefs_;
    float pendingSetpoint_ = 0.0f;
    bool setpointDirty_ = false;
    uint32_t setpointChangedMs_ = 0;
};
