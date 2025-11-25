#include "display.h"
#include "ansi.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

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

static const char * const VEHICLE_TYPES[] = {
    [VEHICLE_LIGHT] = "LIGHT",
    [VEHICLE_HEAVY] = "HEAVY"
};

/**
 * @brief Clear display by printing blank lines
 */
static void clear_display(void)
{
    /* Clear screen using ANSI codes */
    printk("\033[2J\033[H");
}

/**
 * @brief Render display header section
 */
static void render_header(void)
{
    printk("\n");
    printk("═══════════════════════════════════════\n");
    printk(ANSI_BOLD "       SPEED RADAR SYSTEM" ANSI_RESET "\n");
    printk("═══════════════════════════════════════\n");
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
    
    printk("\n");
    printk("  Vehicle:  " ANSI_CYAN "%s" ANSI_RESET "\n", VEHICLE_TYPES[type]);
    printk("  Speed:    " ANSI_CYAN "%u km/h" ANSI_RESET "\n", speed);
    printk("  Limit:    " ANSI_CYAN "%u km/h" ANSI_RESET "\n", check->limit);
    printk("  Status:   %s%s" ANSI_RESET "\n", style->color, style->label);
    printk("\n");
    printk("═══════════════════════════════════════\n\n");
}

void display_show(uint8_t speed, vehicle_type_t type)
{
    /* Get speed check result */
    speed_check_t check = check_speed(speed, type);

    /* Clear and render display */
    clear_display();
    render_header();
    render_info(speed, type, &check);
}