#pragma once

#include <U8g2lib.h>

#include "Status.h"

// 0.42" 72x40 SSD1306. The panel maps to a 72x40 window inside the SSD1306's
// 132x64 RAM; U8g2's dedicated 72X40_ER variant handles the offset. Full
// frame buffer is only 360 bytes.
//
// Layout:            FAULT screen:
//   ┌────────────┐     ┌────────────┐
//   │ 93.1°  reg │     │   FAULT    │
//   │ s93.0 B99.2│     │ boiler NTC │
//   │ ▮▮▮▯▯ 13%  │     │   open     │
//   └────────────┘     └────────────┘
class DisplayUi {
public:
    void begin();
    void render(const Status& st);

private:
    U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2_{U8G2_R0, U8X8_PIN_NONE};
};
