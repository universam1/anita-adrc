#pragma once

#include <Arduino.h>
#include <functional>

#include "CommandParser.h"

// Line-based tuning console on USB serial (see docs/tuning-hardware.md).
// Poll update() from the comms task; accepted commands are handed to
// onCommand, errors are echoed back as "#ERR ..." so they land in the
// capture file too.
class SerialConsole {
public:
    std::function<void(const anita::Command&)> onCommand;

    void update();

private:
    char buf_[96];
    size_t len_ = 0;
};
