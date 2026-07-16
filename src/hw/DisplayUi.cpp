#include "DisplayUi.h"

#include <Wire.h>
#include <cstdio>

#include "config.h"

using anita::Fault;
using anita::State;

void DisplayUi::begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    u8g2_.begin();
    u8g2_.setContrast(255);
}

void DisplayUi::render(const Status& st) {
    char buf[20];
    u8g2_.clearBuffer();

    if (st.fault != Fault::None) {
        u8g2_.setFont(u8g2_font_7x14B_tf);
        u8g2_.drawStr(10, 14, "FAULT");
        u8g2_.setFont(u8g2_font_5x8_tf);
        u8g2_.drawStr(0, 28, anita::faultName(st.fault));
        u8g2_.sendBuffer();
        return;
    }

    if (st.state == State::Boot) {
        u8g2_.setFont(u8g2_font_6x12_tf);
        u8g2_.drawStr(6, 24, "booting");
        u8g2_.sendBuffer();
        return;
    }

    // Line 1: group temperature, big, plus state glyph
    u8g2_.setFont(u8g2_font_logisoso16_tf);
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(st.groupC));
    u8g2_.drawStr(0, 16, buf);
    u8g2_.setFont(u8g2_font_5x8_tf);
    u8g2_.drawStr(48, 8, st.drawActive ? "brew" : anita::stateName(st.state));

    // Line 2: setpoint and boiler temp
    std::snprintf(buf, sizeof(buf), "s%.1f B%.0f", static_cast<double>(st.setpointC),
                  static_cast<double>(st.boilerC));
    u8g2_.drawStr(0, 28, buf);

    // Line 3: duty bar + percent
    const int barW = 44;
    const int fill = static_cast<int>(st.duty * barW + 0.5f);
    u8g2_.drawFrame(0, 32, barW, 7);
    u8g2_.drawBox(0, 32, fill, 7);
    std::snprintf(buf, sizeof(buf), "%2.0f%%", static_cast<double>(st.duty * 100.0f));
    u8g2_.drawStr(barW + 4, 39, buf);

    u8g2_.sendBuffer();
}
