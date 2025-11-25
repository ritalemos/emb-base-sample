/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include "camera_service.h"

ZBUS_MSG_SUBSCRIBER_DEFINE(msub_camera_evt);

ZBUS_CHAN_ADD_OBS(chan_camera_evt, msub_camera_evt, 3);

int main(void)
{
	int err;
	const struct zbus_channel *chan;

	while (1) {
		k_msleep(1000);
		err = camera_api_capture(K_FOREVER);
		if (err) {
			printk("Could not init capture. Error: %d\n", err);
			continue;
		}

		struct msg_camera_evt rsp;

		err = zbus_sub_wait_msg(&msub_camera_evt, &chan, &rsp, K_FOREVER);
		if (err) {
			printk("ERROR: %d\n", err);
			continue;
		}

		if (rsp.type == MSG_CAMERA_EVT_TYPE_ERROR) {
			printk("Camera service unavailable. Error code %d\n", rsp.error_code);
		} else if (rsp.type == MSG_CAMERA_EVT_TYPE_DATA) {
			printf("Current camera data: plate=%s, hash=%s\n", rsp.captured_data->plate,
			       rsp.captured_data->hash);
		}
	}
	return 0;
}