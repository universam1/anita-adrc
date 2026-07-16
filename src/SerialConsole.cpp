#include "SerialConsole.h"

void SerialConsole::update() {
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (len_ == 0) continue;
            buf_[len_] = '\0';
            len_ = 0;
            const anita::Command cmd = anita::CommandParser::parse(buf_);
            if (cmd.type == anita::Command::Type::Invalid) {
                Serial.printf("#ERR %s\n", cmd.error ? cmd.error : "invalid");
            } else if (cmd.type != anita::Command::Type::None) {
                if (onCommand) onCommand(cmd);
            }
        } else if (len_ < sizeof(buf_) - 1) {
            buf_[len_++] = c;
        } else {
            len_ = 0;  // overlong line: drop it
            Serial.println("#ERR line too long");
        }
    }
}
