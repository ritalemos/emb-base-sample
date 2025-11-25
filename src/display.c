#include "display.h"
#include "ansi.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);

/* Display styling configuration */
typedef struct {
    const char *label;
    const char *color;
} status_style_t;

static const status_style_t STATUS_STYLES[] = {
    [STATUS_NORMAL]    = {"NORMAL",    ANSI_GREEN},
    [STATUS_WARNING]   = {"WARNING",   ANSI_YELLOW},
    [STATUS_VIOLATION] = {"VIOLATION", ANSI_RED}
};

static const char *VEHICLE_TYPES[] = {
    [VEHICLE_LIGHT] = "LIGHT",
    [VEHICLE_HEAVY] = "HEAVY"
};

/**
 * @brief Render display header section
 */
static void render_header(void)
{
    printf("\n");
    printf("═══════════════════════════════════════\n");
    printf(ANSI_BOLD "       SPEED RADAR SYSTEM" ANSI_RESET "\n");
    printf("═══════════════════════════════════════\n");
}

/**
 * @brief Render vehicle information section
 * @param speed Vehicle speed in km/h
 * @param type Vehicle type
 * @param check Speed check result containing limit and status
 */
static void render_info(uint8_t speed, vehicle_type_t type, const speed_check_t *check)
{
    const status_style_t *style = &STATUS_STYLES[check->status];
    
    printf("\n");
    printf("  Vehicle:  " ANSI_CYAN "%s" ANSI_RESET "\n", VEHICLE_TYPES[type]);
    printf("  Speed:    " ANSI_CYAN "%3d km/h" ANSI_RESET "\n", speed);
    printf("  Limit:    " ANSI_CYAN "%3d km/h" ANSI_RESET "\n", check->limit);
    printf("  Status:   %s%s" ANSI_RESET "\n", style->color, style->label);
    printf("\n");
    printf("═══════════════════════════════════════\n\n");
}

void display_show(uint8_t speed, vehicle_type_t type)
{
    /* Get speed check result */
    speed_check_t check = check_speed(speed, type);
    const status_style_t *style = &STATUS_STYLES[check.status];

    /* Render display */
    render_header();
    render_info(speed, type, &check);
    fflush(stdout);

    /* Log event */
    LOG_INF("[%s] %d km/h | Limit: %d | %s",
            VEHICLE_TYPES[type], speed, check.limit, style->label);
}