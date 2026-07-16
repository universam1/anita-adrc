#pragma once

#include <cstddef>

namespace anita {

// Parses the serial tuning-console commands (see docs/tuning-hardware.md):
//
//   id duty <d> <seconds>   guarded identification: hold fixed duty
//   id off <seconds>        guarded identification: heater forced off
//   id stop                 abort identification
//   mark <text>             inject a #MARK line into the CSV stream
//   set <param> <value>     live tuning: b0 wc wo pred kboost cap setpoint
//   get                     dump #PARAMS
//
// Pure logic, host-testable. Values are bounds-checked here so the firmware
// side can apply accepted commands without further validation.
struct Command {
    enum class Type {
        None,     // empty line
        IdDuty,
        IdOff,
        IdStop,
        Mark,
        Set,
        Get,
        Invalid,
    };
    enum class Param { None, B0Gain, Wc, Wo, Pred, KBoost, Cap, Setpoint };

    Type type = Type::None;
    Param param = Param::None;
    float value = 0.0f;    // duty for IdDuty, value for Set
    float seconds = 0.0f;  // duration for IdDuty / IdOff
    char text[48] = {};    // Mark label
    const char* error = nullptr;  // set when type == Invalid
};

class CommandParser {
public:
    static Command parse(const char* line);
};

}  // namespace anita
