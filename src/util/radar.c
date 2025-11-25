#include "radar.h"
#include <zephyr/kernel.h>

/* Speed limit configuration table */
static const uint8_t speed_limits[] = {
    [VEHICLE_LIGHT] = CONFIG_RADAR_SPEED_LIMIT_LIGHT_KMH,
    [VEHICLE_HEAVY] = CONFIG_RADAR_SPEED_LIMIT_HEAVY_KMH
};

/**
 * @brief Calculate warning threshold based on limit percentage
 * @param limit Speed limit in km/h
 * @return Warning threshold in km/h
 */
static inline uint8_t get_warning_threshold(uint8_t limit)
{
    return (limit * CONFIG_RADAR_WARNING_THRESHOLD_PERCENT) / 100;
}

/**
 * @brief Evaluate speed status based on limit and threshold
 * @param speed Measured speed in km/h
 * @param limit Speed limit in km/h
 * @param threshold Warning threshold in km/h
 * @return Speed status (NORMAL, WARNING, or VIOLATION)
 */
static speed_status_t evaluate_status(uint8_t speed, uint8_t limit, uint8_t threshold)
{
    if (speed > limit) {
        return STATUS_VIOLATION;
    }
    if (speed >= threshold) {
        return STATUS_WARNING;
    }
    return STATUS_NORMAL;
}

speed_check_t check_speed(uint8_t speed, vehicle_type_t type)
{
    speed_check_t result;

    /* Validate vehicle type */
    if (type >= ARRAY_SIZE(speed_limits)) {
        result.limit = 0;
        result.status = STATUS_VIOLATION;
        return result;
    }

    /* Get limit and evaluate */
    result.limit = speed_limits[type];
    uint8_t threshold = get_warning_threshold(result.limit);
    result.status = evaluate_status(speed, result.limit, threshold);

    return result;
}