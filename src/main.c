#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct device* const console_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static struct gpio_callback button_cb_data;

#define NUM_STEPS       50U
#define SW_PWM_PERIOD_US 20000U
#define BLINK_SLEEP_MS  500U
#define FADE_SLEEP_MSEC 25U
#define FADE_STEP_US (SW_PWM_PERIOD_US / NUM_STEPS)

enum operation_mode {
    MODE_BLINK = 0,
    MODE_PWM_FADE
};

static struct {
    volatile enum operation_mode current_mode;
    uint32_t pulse_width_us;
    uint8_t fade_dir;
    uint32_t last_log_pulse_us;
} self = {
    .current_mode = MODE_BLINK,
};

void run_blink(void) {
	static bool led_is_on = true;

	gpio_pin_toggle_dt(&led0);

	led_is_on = !led_is_on;
	LOG_INF("Modo Blink - LED: %s", led_is_on ? "ON" : "OFF");

	k_msleep(BLINK_SLEEP_MS);
}

void sw_pwm_cycle(void) {
	gpio_pin_set_dt(&led0, 1);
	if (self.pulse_width_us > 0) {
		k_busy_wait(self.pulse_width_us);
	}

	gpio_pin_set_dt(&led0, 0);
	if (SW_PWM_PERIOD_US - self.pulse_width_us > 0) {
		k_busy_wait(SW_PWM_PERIOD_US - self.pulse_width_us);
	}
}


void run_pwm_fade(void) {
	sw_pwm_cycle();
	LOG_INF("Modo Fade: Brilho em ~%d%%", (100 * self.pulse_width_us) / SW_PWM_PERIOD_US);

	if (self.fade_dir == 1) {
		if (self.pulse_width_us + FADE_STEP_US >= SW_PWM_PERIOD_US) {
			self.pulse_width_us = SW_PWM_PERIOD_US;
			self.fade_dir = 0U;
			LOG_INF("Modo Fade - Brilho máximo (100%%), iniciando Fade-Out...");
		} else {
			self.pulse_width_us += FADE_STEP_US;
		}
	} else {
		if (self.pulse_width_us <= FADE_STEP_US) {
			self.pulse_width_us = 0;
			self.fade_dir = 1U;
			LOG_INF("Modo Fade - Brilho mínimo (0%%), iniciando Fade-In...");
		} else {
			self.pulse_width_us -= FADE_STEP_US;
		}
	}

	k_msleep(FADE_SLEEP_MSEC);
}

void button_pressed(const struct device* dev, struct gpio_callback* cb, uint32_t pins) {
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    if (self.current_mode == MODE_BLINK) {
        self.current_mode = MODE_PWM_FADE;
        self.pulse_width_us = 0;
        self.fade_dir = 1U;
        LOG_INF("--- Botão pressionado! Modo alterado para: FADE ---");
    } else {
        self.current_mode = MODE_BLINK;
        gpio_pin_set_dt(&led0, 0);
        LOG_INF("--- Botão pressionado! Modo alterado para: BLINK ---");
    }
}

int main(void) {
    unsigned char c;

    if (!gpio_is_ready_dt(&led0) || !gpio_is_ready_dt(&button)) {
        return 0;
    }

    gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);

    gpio_pin_configure_dt(&button, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    LOG_INF("Pressione ENTER para trocar de modo.");
    LOG_INF("Modo inicial: BLINK");
	LOG_INF("-------------------------------------------------");

    
    while (1) {
        if (!uart_poll_in(console_dev, &c) && (c == '\n' || c == '\r')) {
            /* Simulando a interrupção. */
            button_pressed(button.port, &button_cb_data, BIT(button.pin));
        }


        switch (self.current_mode) {
            case MODE_PWM_FADE:
                run_pwm_fade();
                break;
            case MODE_BLINK:
                run_blink();
                break;
        }
    }
    
    return 0;
}