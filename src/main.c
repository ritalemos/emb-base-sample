#include "zephyr/logging/log_core.h"
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(hello_world, LOG_LEVEL_DBG);

void timer_manager(struct k_timer *dummy)
{
	static uint8_t count = 1;

	printk("hello world :)\n");

    if(count == 1) {
        LOG_DBG("printou 'hello world' %d vez\n", count);
    } else {
        LOG_DBG("printou 'hello world' %d vezes\n", count);
    }

	if (count == UINT8_MAX) {
		LOG_ERR("count >= 255, reiniciando timer...");

		count = 0;
	}

	count++;
}

K_TIMER_DEFINE(hello_world_timer, timer_manager, NULL);

int main(void)
{
	LOG_INF("Atividade 1 - Timer com Hello World\n");

	LOG_DBG("Timer 'Hello World': primeira execução programada para %d ms.\n", CONFIG_HELLO_WORLD_TIMER_START);
    LOG_DBG("Timer 'Hello World': intervalo entre execuções definido em %d ms.\n", CONFIG_HELLO_WORLD_TIMER_INTERVAL);

	k_timer_start(&hello_world_timer, K_MSEC(CONFIG_HELLO_WORLD_TIMER_START),
		      K_MSEC(CONFIG_HELLO_WORLD_TIMER_INTERVAL));

	return 0;
}
