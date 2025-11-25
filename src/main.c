#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zbus/zbus.h>
#include "display.h"
#include "radar.h"
#include "mercosul_plate.h"
#include "camera_service.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* ============================================================================
 * CONFIGURATION CONSTANTS
 * ============================================================================ */
#define VEHICLE_TIMEOUT_MS  1500   /* Axle detection timeout */
#define SPEED_TIMEOUT_MS    5000   /* Speed measurement timeout */
#define MIN_SPEED_DT_MS     50     /* Minimum time between sensors */
#define MAX_SPEED_DT_MS     15000  /* Maximum time between sensors */

#define SENSOR_POLL_MS      20     /* Sensor queue polling interval */
#define MAIN_LOOP_DELAY_MS  20     /* Main loop delay */

#define MAX_SPEED_KMH       200    /* Maximum valid speed */

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================ */

/* Radar state machine states */
enum radar_state {
    STATE_IDLE = 0,
    STATE_READING,
    STATE_DONE
};

/* Sensor event types */
enum sensor_event {
    EVENT_SENSOR_0 = 0,  /* Axle detection sensor */
    EVENT_SENSOR_1,      /* Speed measurement sensor */
    EVENT_TIMEOUT        /* Timer expired */
};

/* Sensor event data */
struct sensor_data {
    enum sensor_event event;
    uint32_t timestamp;
};

/* Vehicle detection result */
struct vehicle_data {
    uint8_t speed;
    vehicle_type_t type;
    uint8_t axle_count;
};

/* Display message structure */
struct display_msg {
    uint8_t speed;
    vehicle_type_t type;
};

/* System statistics */
struct detection_stats {
    uint32_t total_detections;
    uint32_t speed_violations;
    uint32_t invalid_plates;
};

/* Vehicle detection context */
struct detection_context {
    uint8_t axle_count;
    uint32_t start_timestamp;
    bool speed_calculated;
};

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================ */
void vehicle_timeout_handler(struct k_timer *timer);

/* ============================================================================
 * GLOBAL VARIABLES
 * ============================================================================ */

/* Message queues */
K_MSGQ_DEFINE(sensor_queue, sizeof(struct sensor_data), 10, 4);
K_MSGQ_DEFINE(vehicle_queue, sizeof(struct vehicle_data), 10, 4);
K_MSGQ_DEFINE(display_queue, sizeof(struct display_msg), 10, 4);

/* ZBus subscriber for camera events */
ZBUS_MSG_SUBSCRIBER_DEFINE(msub_camera_evt);
ZBUS_CHAN_ADD_OBS(chan_camera_evt, msub_camera_evt, 3);

/* Console device */
static const struct device *const console_dev = 
    DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* GPIO sensors */
static const struct gpio_dt_spec sensor0 = 
    GPIO_DT_SPEC_GET(DT_ALIAS(button0), gpios);
static const struct gpio_dt_spec sensor1 = 
    GPIO_DT_SPEC_GET(DT_ALIAS(button1), gpios);

static struct gpio_callback sensor0_cb_data;
static struct gpio_callback sensor1_cb_data;

/* State machine */
static enum radar_state current_state = STATE_IDLE;
static struct detection_stats stats = {0};

/* Timer */
K_TIMER_DEFINE(vehicle_timeout, vehicle_timeout_handler, NULL);

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

/**
 * @brief Validate time delta for speed calculation
 * @param dt_ms Time delta in milliseconds
 * @return true if valid, false otherwise
 */
static inline bool is_valid_time_delta(uint32_t dt_ms)
{
    return (dt_ms >= MIN_SPEED_DT_MS && dt_ms <= MAX_SPEED_DT_MS);
}

/**
 * @brief Validate speed range
 * @param speed Speed in km/h
 * @return true if valid, false otherwise
 */
static inline bool is_valid_speed(uint32_t speed)
{
    return (speed > 0 && speed <= MAX_SPEED_KMH);
}

/**
 * @brief Calculate speed from time delta
 * @param dt_ms Time delta in milliseconds
 * @return Speed in km/h, or 0 if invalid
 */
static uint8_t calculate_speed(uint32_t dt_ms)
{
    if (!is_valid_time_delta(dt_ms)) {
        return 0;
    }

    /* Formula: v = (distance / time) * 3.6 
     * distance in mm, time in ms -> result in km/h */
    uint32_t speed = (CONFIG_RADAR_SENSOR_DISTANCE_MM * 3600UL) / 
                     (dt_ms * 1000UL);

    if (!is_valid_speed(speed)) {
        return 0;
    }

    return (uint8_t)speed;
}

/**
 * @brief Classify vehicle based on axle count
 * @param axle_count Number of detected axles
 * @param type Output parameter for vehicle type
 * @return true if valid classification, false otherwise
 */
static bool classify_vehicle(uint8_t axle_count, vehicle_type_t *type)
{
    if (axle_count == 2) {
        *type = VEHICLE_LIGHT;
        return true;
    } 
    
    if (axle_count >= 3) {
        *type = VEHICLE_HEAVY;
        return true;
    }
    
    return false;
}

/**
 * @brief Reset detection context
 * @param ctx Detection context to reset
 */
static inline void reset_detection_context(struct detection_context *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

/**
 * @brief Start detection timeout timer
 * @param timeout_ms Timeout in milliseconds
 */
static void start_detection_timer(uint32_t timeout_ms)
{
    k_timer_stop(&vehicle_timeout);
    k_timer_start(&vehicle_timeout, K_MSEC(timeout_ms), K_NO_WAIT);
}

/* ============================================================================
 * SENSOR EVENT HANDLERS
 * ============================================================================ */

/**
 * @brief Handle first axle detection (start of vehicle)
 * @param ctx Detection context
 * @param timestamp Event timestamp
 */
static void handle_first_axle(struct detection_context *ctx, uint32_t timestamp)
{
    ctx->axle_count = 1;
    ctx->start_timestamp = timestamp;
    current_state = STATE_READING;
    start_detection_timer(VEHICLE_TIMEOUT_MS);
    stats.total_detections++;
}

/**
 * @brief Handle additional axle detection
 * @param ctx Detection context
 */
static void handle_additional_axle(struct detection_context *ctx)
{
    ctx->axle_count++;
    start_detection_timer(VEHICLE_TIMEOUT_MS);
}

/**
 * @brief Handle speed sensor trigger
 * @param ctx Detection context
 * @param vehicle Vehicle data to update
 * @param timestamp Event timestamp
 */
static void handle_speed_sensor(struct detection_context *ctx,
                                struct vehicle_data *vehicle,
                                uint32_t timestamp)
{
    if (ctx->speed_calculated) {
        return;
    }

    uint32_t dt = timestamp - ctx->start_timestamp;
    vehicle->speed = calculate_speed(dt);
    
    if (vehicle->speed > 0) {
        ctx->speed_calculated = true;
    }
    
    /* Extend timeout to allow final axle detection */
    start_detection_timer(SPEED_TIMEOUT_MS);
}

/**
 * @brief Process completed detection
 * @param ctx Detection context
 * @param vehicle Vehicle data
 * @return true if valid detection, false otherwise
 */
static bool process_completed_detection(struct detection_context *ctx,
                                        struct vehicle_data *vehicle)
{
    /* Validate speed measurement */
    if (!ctx->speed_calculated) {
        return false;
    }

    /* Classify vehicle */
    if (!classify_vehicle(ctx->axle_count, &vehicle->type)) {
        return false;
    }

    vehicle->axle_count = ctx->axle_count;
    return true;
}

/* ============================================================================
 * GPIO CALLBACKS
 * ============================================================================ */

/**
 * @brief Sensor 0 interrupt callback (axle detection)
 */
static void sensor0_callback(const struct device *dev, 
                             struct gpio_callback *cb, 
                             uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    struct sensor_data data = {
        .event = EVENT_SENSOR_0,
        .timestamp = k_uptime_get_32()
    };
    k_msgq_put(&sensor_queue, &data, K_NO_WAIT);
}

/**
 * @brief Sensor 1 interrupt callback (speed measurement)
 */
static void sensor1_callback(const struct device *dev, 
                             struct gpio_callback *cb, 
                             uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    struct sensor_data data = {
        .event = EVENT_SENSOR_1,
        .timestamp = k_uptime_get_32()
    };
    k_msgq_put(&sensor_queue, &data, K_NO_WAIT);
}

/**
 * @brief Timeout handler - vehicle passed
 */
void vehicle_timeout_handler(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    struct sensor_data data = {
        .event = EVENT_TIMEOUT,
        .timestamp = k_uptime_get_32()
    };
    k_msgq_put(&sensor_queue, &data, K_NO_WAIT);
}

/* ============================================================================
 * CAMERA HANDLING
 * ============================================================================ */

/**
 * @brief Print violation record
 * @param plate License plate
 * @param hash Plate hash
 * @param is_valid Plate validity
 */
static void print_violation_record(const char *plate, 
                                   const char *hash, 
                                   bool is_valid)
{
    printk("\n");
    printk("================================================================================\n");
    printk("                            VIOLATION RECORD\n");
    printk("================================================================================\n");
    printk("Plate:  %s\n", plate);
    printk("Hash:   %s\n", hash);
    printk("Status: %s\n", is_valid ? 
           "✓ Valid Mercosul plate" : "✗ Invalid plate format");
    printk("================================================================================\n");
    printk("\n");
}

/**
 * @brief Handle camera capture and validation
 * @return 0 on success, negative error code otherwise
 */
static int handle_camera_capture(void)
{
    const struct zbus_channel *chan;
    struct msg_camera_evt rsp;
    int err;

    /* Initiate camera capture */
    err = camera_api_capture(K_FOREVER);
    if (err) {
        printk("\n");
        printk("================================================================================\n");
        printk("Camera capture error: %d\n", err);
        printk("================================================================================\n");
        printk("\n");
        return err;
    }

    /* Wait for camera response */
    err = zbus_sub_wait_msg(&msub_camera_evt, &chan, &rsp, K_FOREVER);
    if (err) {
        printk("\n");
        printk("================================================================================\n");
        printk("Camera event error: %d\n", err);
        printk("================================================================================\n");
        printk("\n");
        return err;
    }

    /* Handle camera error */
    if (rsp.type == MSG_CAMERA_EVT_TYPE_ERROR) {
        printk("\n");
        printk("================================================================================\n");
        printk("Camera service unavailable - Error code: %d\n", rsp.error_code);
        printk("================================================================================\n");
        printk("\n");
        return -EIO;
    }

    /* Process captured data */
    if (rsp.type == MSG_CAMERA_EVT_TYPE_DATA) {
        const char *plate = rsp.captured_data->plate;
        const char *hash = rsp.captured_data->hash;
        bool is_valid = mercosul_plate_is_valid(plate);

        if (!is_valid) {
            stats.invalid_plates++;
        }

        print_violation_record(plate, hash, is_valid);
    }

    return 0;
}

/* ============================================================================
 * THREAD FUNCTIONS
 * ============================================================================ */

/**
 * @brief Sensor processing thread
 * Monitors GPIO interrupts, implements state machine for axle counting
 * and speed measurement. Sends vehicle data to control thread via message queue.
 */
static void sensor_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    struct sensor_data sensor_event;
    struct vehicle_data vehicle = {0};
    struct detection_context ctx = {0};

    while (1) {
        /* Wait for sensor event */
        if (current_state != STATE_DONE) {
            int err = k_msgq_get(&sensor_queue, &sensor_event, 
                                K_MSEC(SENSOR_POLL_MS));
            if (err) {
                continue;
            }
        }

        /* State machine */
        switch (current_state) {
        case STATE_IDLE:
            if (sensor_event.event == EVENT_SENSOR_0) {
                handle_first_axle(&ctx, sensor_event.timestamp);
            }
            break;

        case STATE_READING:
            if (sensor_event.event == EVENT_SENSOR_0) {
                handle_additional_axle(&ctx);
            } else if (sensor_event.event == EVENT_SENSOR_1) {
                handle_speed_sensor(&ctx, &vehicle, sensor_event.timestamp);
            } else if (sensor_event.event == EVENT_TIMEOUT) {
                current_state = STATE_DONE;
            }
            break;

        case STATE_DONE:
            /* Process detection */
            if (process_completed_detection(&ctx, &vehicle)) {
                k_msgq_put(&vehicle_queue, &vehicle, K_NO_WAIT);
            }

            /* Reset for next detection */
            current_state = STATE_IDLE;
            reset_detection_context(&ctx);
            memset(&vehicle, 0, sizeof(vehicle));
            break;
        }
    }
}

/**
 * @brief Control thread - orchestrates system
 * Receives vehicle data, detects violations (applying correct limits),
 * triggers camera, validates plates, and sends display updates.
 */
static void control_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    struct vehicle_data vehicle;
    struct display_msg display_data;

    while (1) {
        /* Wait for vehicle data from sensor thread */
        int err = k_msgq_get(&vehicle_queue, &vehicle, K_FOREVER);
        if (err) {
            continue;
        }

        /* Prepare display message */
        display_data.speed = vehicle.speed;
        display_data.type = vehicle.type;

        /* Send to display thread */
        k_msgq_put(&display_queue, &display_data, K_NO_WAIT);

        /* Check for speed violations */
        speed_check_t check = check_speed(vehicle.speed, vehicle.type);
        
        if (check.status == STATUS_VIOLATION) {
            stats.speed_violations++;
            handle_camera_capture();
        }
    }
}

/**
 * @brief Display thread - updates virtual display
 * Receives display messages and formats output with ANSI colors
 * based on status (Normal, Warning, Violation).
 */
static void display_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    struct display_msg msg;

    while (1) {
        /* Wait for display update message from control thread */
        if (k_msgq_get(&display_queue, &msg, K_FOREVER) == 0) {
            /* Update display with vehicle information */
            display_show(msg.speed, msg.type);
        }
    }
}

/* ============================================================================
 * USER INTERFACE
 * ============================================================================ */

/**
 * @brief Print system statistics
 */
static void print_statistics(void)
{
    printk("\n");
    printk("================================================================================\n");
    printk("                           DETECTION STATISTICS\n");
    printk("================================================================================\n");
    printk("Total Detections:  %u\n", stats.total_detections);
    printk("Speed Violations:  %u\n", stats.speed_violations);
    printk("Invalid Plates:    %u\n", stats.invalid_plates);
    printk("================================================================================\n");
    printk("\n");
}

/**
 * @brief Print system banner
 */
static void print_banner(void)
{
    printk("\n");
    printk("================================================================================\n");
    printk("                           SPEED RADAR SYSTEM\n");
    printk("================================================================================\n");
    printk("Sensor Distance: %d mm\n", CONFIG_RADAR_SENSOR_DISTANCE_MM);
    printk("Light Limit:     %d km/h\n", CONFIG_RADAR_SPEED_LIMIT_LIGHT_KMH);
    printk("Heavy Limit:     %d km/h\n", CONFIG_RADAR_SPEED_LIMIT_HEAVY_KMH);
    printk("================================================================================\n");
    printk("Controls:\n");
    printk("  Press 's' → Show statistics\n");
    printk("================================================================================\n");
    printk("\n");
}

/* ============================================================================
 * GPIO SETUP
 * ============================================================================ */

/**
 * @brief Setup GPIO sensor
 * @param spec GPIO specification
 * @param cb_data Callback data
 * @param callback Callback function
 * @return 0 on success, negative error code otherwise
 */
static int setup_gpio_sensor(const struct gpio_dt_spec *spec,
                             struct gpio_callback *cb_data,
                             gpio_callback_handler_t callback)
{
    int ret;

    ret = gpio_pin_configure_dt(spec, GPIO_INPUT);
    if (ret < 0) {
        return ret;
    }

    gpio_init_callback(cb_data, callback, BIT(spec->pin));
    gpio_add_callback(spec->port, cb_data);

    ret = gpio_pin_interrupt_configure_dt(spec, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

/**
 * @brief Initialize GPIO sensors
 * @return 0 on success, negative error code otherwise
 */
static int init_gpio_sensors(void)
{
    int ret;

    /* Check GPIO devices */
    if (!gpio_is_ready_dt(&sensor0) || !gpio_is_ready_dt(&sensor1)) {
        LOG_ERR("GPIO devices not ready");
        return -ENODEV;
    }

    /* Setup sensor 0 */
    ret = setup_gpio_sensor(&sensor0, &sensor0_cb_data, sensor0_callback);
    if (ret < 0) {
        LOG_ERR("Failed to setup sensor 0: %d", ret);
        return ret;
    }

    /* Setup sensor 1 */
    ret = setup_gpio_sensor(&sensor1, &sensor1_cb_data, sensor1_callback);
    if (ret < 0) {
        LOG_ERR("Failed to setup sensor 1: %d", ret);
        return ret;
    }

    return 0;
}

/* ============================================================================
 * INPUT HANDLING
 * ============================================================================ */

/**
 * @brief Process keyboard input
 * @param key Pressed key
 */
static void process_keyboard_input(uint8_t key)
{
    switch (key) {
    case '1':
        /* Simulate Sensor 0 (axle detection) */
        gpio_emul_input_set(sensor0.port, sensor0.pin, 1);
        gpio_emul_input_set(sensor0.port, sensor0.pin, 0);
        break;
        
    case '2':
        /* Simulate Sensor 1 (speed measurement) */
        gpio_emul_input_set(sensor1.port, sensor1.pin, 1);
        gpio_emul_input_set(sensor1.port, sensor1.pin, 0);
        break;
        
    case 's':
    case 'S':
        print_statistics();
        break;
    }
}

/* ============================================================================
 * MAIN FUNCTION
 * ============================================================================ */

/**
 * @brief Main application entry point
 */
int main(void)
{
    uint8_t key;
    int ret;

    /* Print system banner */
    print_banner();

    /* Initialize GPIO sensors */
    ret = init_gpio_sensors();
    if (ret < 0) {
        LOG_ERR("Failed to initialize GPIO sensors: %d", ret);
        return ret;
    }

    /* Main loop - keyboard input simulation */
    while (1) {
        if (!uart_poll_in(console_dev, &key)) {
            process_keyboard_input(key);
        }
        k_msleep(MAIN_LOOP_DELAY_MS);
    }

    return 0;
}

/* ============================================================================
 * THREAD DEFINITIONS
 * ============================================================================ */
K_THREAD_DEFINE(sensor_thread_id, 1024, sensor_thread, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(control_thread_id, 2048, control_thread, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(display_thread_id, 1024, display_thread, NULL, NULL, NULL, 5, 0, 0);