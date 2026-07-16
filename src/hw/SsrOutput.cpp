#include "SsrOutput.h"

#include "config.h"

void SsrOutput::begin() {
    // Park the output register HIGH (off) before enabling the driver so the
    // pin never glitches low during init.
    digitalWrite(PIN_SSR, SSR_OFF_LEVEL);
    pinMode(PIN_SSR, OUTPUT);
    digitalWrite(PIN_SSR, SSR_OFF_LEVEL);

    const esp_timer_create_args_t args = {
        .callback = &SsrOutput::onTick,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ssr_tick",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_, SSR_TICK_US));
}

void SsrOutput::onTick(void* self) {
    auto* s = static_cast<SsrOutput*>(self);
    bool on;
    portENTER_CRITICAL(&s->mux_);
    on = s->mod_.tick();
    portEXIT_CRITICAL(&s->mux_);
    digitalWrite(PIN_SSR, on ? SSR_ON_LEVEL : SSR_OFF_LEVEL);
}

void SsrOutput::setDuty(float d) {
    portENTER_CRITICAL(&mux_);
    mod_.setDuty(d);
    portEXIT_CRITICAL(&mux_);
}

float SsrOutput::consumeActualDuty() {
    portENTER_CRITICAL(&mux_);
    const float d = mod_.consumeActualDuty();
    portEXIT_CRITICAL(&mux_);
    return d;
}

void SsrOutput::forceOff() {
    setDuty(0.0f);
    digitalWrite(PIN_SSR, SSR_OFF_LEVEL);
}
