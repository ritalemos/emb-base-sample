#ifndef RADAR_H
#define RADAR_H

#include <stdint.h>

typedef enum {
    VEHICLE_LIGHT,
    VEHICLE_HEAVY
} vehicle_type_t;

typedef enum {
    STATUS_NORMAL,
    STATUS_WARNING,
    STATUS_VIOLATION
} speed_status_t;

typedef struct {
    uint8_t limit;
    speed_status_t status;
} speed_check_t;

speed_check_t check_speed(uint8_t speed, vehicle_type_t type);

#endif /* RADAR_H */