#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include "display.h"
#include "radar.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Speed simulation parameters */
#define SPEED_MIN_KMH 30
#define SPEED_MAX_KMH 120
#define READING_INTERVAL_S 2

/* Vehicle detection sequence */
static const vehicle_type_t detection_sequence[] = {
    VEHICLE_LIGHT,
    VEHICLE_HEAVY,
    VEHICLE_HEAVY
};

/**
 * @brief Generate simulated speed reading
 * @return Random speed value between SPEED_MIN_KMH and SPEED_MAX_KMH
 */
static uint8_t simulate_speed_reading(void)
{
    uint32_t range = SPEED_MAX_KMH - SPEED_MIN_KMH + 1;
    return SPEED_MIN_KMH + (sys_rand32_get() % range);
}

/**
 * @brief Process vehicle detection event
 * @param type Type of vehicle detected
 */
static void process_detection(vehicle_type_t type)
{
    uint8_t measured_speed = simulate_speed_reading();
    display_show(measured_speed, type);
    k_sleep(K_SECONDS(READING_INTERVAL_S));
}

/**
 * @brief Main application entry point
 * @return 0 on success
 */
int main(void)
{
    const size_t sequence_len = ARRAY_SIZE(detection_sequence);
    
    LOG_INF("Speed Radar System initialized");
    LOG_INF("Reading interval: %d seconds", READING_INTERVAL_S);
    
    while (1) {
        for (size_t i = 0; i < sequence_len; i++) {
            process_detection(detection_sequence[i]);
        }
    }

    return 0;
}