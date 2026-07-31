// PreemptQueue - real-time ISR -> high-priority task via a preempting queue.
//
// A hardware-timer ISR posts a reading into the preempting queue; a dedicated,
// high-priority, core-pinned task is preempted to immediately and processes the
// reading off the interrupt. Zero heap (static queue), fail-closed when full.
//
// Build flag (must be set for the whole build, not just this sketch):
//   PC_ENABLE_PREEMPT_QUEUE=1
// Optional sizing: PC_PQ_DEPTH, PC_PQ_ITEM_SIZE, PC_PQ_STACK.

#include "protocore.h" // discovers the library (adds src/ to the include path)
#include "esp_timer.h"
#include "services/system/preempt_queue/preempt_queue.h"

struct Reading
{
    uint64_t t_us;  // when the ISR captured it
    uint32_t value; // a fake sample (a real driver would read the sensor here)
};

// Runs in the high-priority processing task, NOT the ISR - so it can do real work
// (printf, math, post to MQTT, ...) without blocking interrupts.
static void on_reading(const void *item, void *)
{
    const Reading *r = (const Reading *)item;
    Serial.printf("reading %u captured at %llu us\n", r->value, (unsigned long long)r->t_us);
}

static hw_timer_t *tmr = nullptr;
static uint32_t g_seq = 0;

// Keep the ISR tiny: timestamp + post. pc_pq_post_from_isr() is interrupt-safe
// and asks the scheduler to switch to the high-priority task the moment we return.
static void IRAM_ATTR sample_isr()
{
    Reading r;
    r.t_us = esp_timer_get_time();
    r.value = g_seq++;
    pc_pq_post_from_isr(&r);
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    pc_pq_config cfg = {};
    cfg.handler = on_reading;
    cfg.priority = 6; // above loop(): a post preempts straight into on_reading
    cfg.core = 1;
    cfg.name = "sampler";
    if (!pc_pq_start(&cfg))
    {
        Serial.println("preempt queue failed to start");
        return;
    }

    // 10 Hz hardware-timer ISR feeding the queue. The Arduino-ESP32 timer API changed in
    // core 3.0 (frequency-based; timerAlarm() replaces timerAlarmWrite()/Enable()).
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    tmr = timerBegin(1000000);              // 1 MHz tick
    timerAttachInterrupt(tmr, &sample_isr); // no edge argument in 3.x
    timerAlarm(tmr, 100000, true, 0);       // 100000 us = 10 Hz, auto-reload, unlimited
#else
    tmr = timerBegin(0, 80, true); // timer 0, /80 -> 1 MHz tick
    timerAttachInterrupt(tmr, &sample_isr, true);
    timerAlarmWrite(tmr, 100000, true); // 100000 us = 10 Hz
    timerAlarmEnable(tmr);
#endif
    Serial.println("sampling: ISR -> preempting queue -> high-priority task");
}

void loop()
{
    // The real-time work happens in the ISR + the high-priority task; loop() is free.
    delay(1000);
}
