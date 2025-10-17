#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(SENSOR);

#define TEMP_MIN 18
#define TEMP_MAX 30

#define HUMIDITY_MIN 40
#define HUMIDITY_MAX 70

#define THREAD_STACK_SIZE 1024
#define THREAD_PRIORITY 3

typedef enum {
    HUMIDITY_SENSOR = 0,
    TEMPERATURE_SENSOR,
} sensor_type;

typedef struct {
    sensor_type sensor;
    union {
        uint8_t humidity_value;
        int8_t temperature_value;
    } data;
} sensor_data;

K_MSGQ_DEFINE(raw_data_queue, sizeof(sensor_data), 10, 4);
K_MSGQ_DEFINE(validated_data_queue, sizeof(sensor_data), 10, 4);

static sensor_data read_sensor(sensor_type sensor_type) {
    sensor_data reading;
    reading.sensor = sensor_type;
    
    if (sensor_type == HUMIDITY_SENSOR) {
        reading.data.humidity_value = sys_rand32_get() % 100;
    } else {
        reading.data.temperature_value = sys_rand32_get() % 100;
    }
    
    return reading;
}

static void produce_humidity_data(void) {
    sensor_data sensor_reading = read_sensor(HUMIDITY_SENSOR);
    k_msgq_put(&raw_data_queue, &sensor_reading, K_FOREVER);
}

static void produce_temperature_data(void) {
    sensor_data sensor_reading = read_sensor(TEMPERATURE_SENSOR);
    k_msgq_put(&raw_data_queue, &sensor_reading, K_FOREVER);
}

int main(void) {
    uint32_t delay_ms = 500;

    LOG_INF("Iniciando leitura dos sensores...");
    
    while (1) {
        produce_humidity_data();
        k_msleep(delay_ms);

        produce_temperature_data();
        k_msleep(delay_ms);
    }

    return 0;
}

static int filter(void *arg1, void *arg2, void *arg3) {
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    sensor_data incoming_data;

    while (1) {
        if (k_msgq_get(&raw_data_queue, &incoming_data, K_FOREVER) == 0) {
            bool is_valid = false;
            
            switch (incoming_data.sensor) {
                case HUMIDITY_SENSOR:
                    if (incoming_data.data.humidity_value >= HUMIDITY_MIN && 
                        incoming_data.data.humidity_value <= HUMIDITY_MAX) {
                        is_valid = true;
                    } else {
                        LOG_ERR("Umidade fora do intervalo valido: %d%%", 
                                incoming_data.data.humidity_value);
                    }
                    break;
                    
                case TEMPERATURE_SENSOR:
                    if (incoming_data.data.temperature_value >= TEMP_MIN && 
                        incoming_data.data.temperature_value <= TEMP_MAX) {
                        is_valid = true;
                    } else {
                        LOG_ERR("Temperatura fora do intervalo valido: %d°C", 
                                incoming_data.data.temperature_value);
                    }
                    break; 
            }
            
            if (is_valid) {
                k_msgq_put(&validated_data_queue, &incoming_data, K_FOREVER);
            }
        }
    }

    return 0;
}

static int consumer(void *arg1, void *arg2, void *arg3) {
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    sensor_data validated_reading;

    while (1) {
        if (k_msgq_get(&validated_data_queue, &validated_reading, K_FOREVER) == 0) {
            if (validated_reading.sensor == HUMIDITY_SENSOR) {
                LOG_INF("Umidade: %d%%", validated_reading.data.humidity_value);
            } else if (validated_reading.sensor == TEMPERATURE_SENSOR) {
                LOG_INF("Temperatura: %d°C", validated_reading.data.temperature_value);
            }
        }
    }

    return 0;
}

K_THREAD_DEFINE(filter_id, THREAD_STACK_SIZE, filter, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
K_THREAD_DEFINE(processor_id, THREAD_STACK_SIZE, consumer, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);