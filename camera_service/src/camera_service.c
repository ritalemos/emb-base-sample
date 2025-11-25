#include "camera_service_priv.h"

#include <zephyr/random/random.h>

struct msg_camera_cmd {
	enum {
		MSG_CAMERA_CMD_TYPE_UNDEFINED,
		MSG_CAMERA_CMD_TYPE_CAPTURE,
		MSG_CAMERA_CMD_TYPE_COUNT
	} type;
};

const size_t valid_array_size = ARRAY_SIZE(valid_car_license_plates);
const size_t invalid_array_size = ARRAY_SIZE(invalid_car_license_plates);

ZBUS_CHAN_DEFINE(chan_camera_cmd, struct msg_camera_cmd, NULL, NULL,
		 ZBUS_OBSERVERS(msub_camera_cmd),
		 ZBUS_MSG_INIT(.type = MSG_CAMERA_CMD_TYPE_UNDEFINED));

ZBUS_CHAN_DEFINE(chan_camera_evt, struct msg_camera_evt, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.type = MSG_CAMERA_EVT_TYPE_UNDEFINED));

ZBUS_MSG_SUBSCRIBER_DEFINE(msub_camera_cmd);

int camera_api_capture(k_timeout_t timeout)
{
	struct msg_camera_cmd msg = {.type = MSG_CAMERA_CMD_TYPE_CAPTURE};

	return zbus_chan_pub(&chan_camera_cmd, &msg, timeout);
}

void camera_thread(void *ptr1, void *ptr2, void *ptr3)
{
	ARG_UNUSED(ptr1);
	ARG_UNUSED(ptr2);
	ARG_UNUSED(ptr3);

	int err;
	const struct zbus_channel *chan;
	struct msg_camera_cmd cmd;

	/* Simulate the camera initialization moment */
	k_busy_wait(340000);

	printk("Camera service started...[ok]\n");

	while (1) {
		err = zbus_sub_wait_msg(&msub_camera_cmd, &chan, &cmd, K_FOREVER);
		if (err) {
			printk("Error code %d in %s (line:%d)\n", err, __FUNCTION__, __LINE__);
			continue;
		}

		struct msg_camera_evt evt;

		switch (cmd.type) {
		case MSG_CAMERA_CMD_TYPE_CAPTURE: {
			/* Simulate the camera taking the pucture it takes from 0 to 256 ms */
			k_busy_wait(sys_rand8_get() * 1000);

			int random_key = sys_rand16_get() % 1100;

			if (random_key < 900) {
				evt.type = MSG_CAMERA_EVT_TYPE_DATA;
				evt.captured_data =
					valid_car_license_plates + (random_key % valid_array_size);
			} else if (900 <= random_key && random_key < 1000) {
				evt.type = MSG_CAMERA_EVT_TYPE_DATA;
				evt.captured_data = invalid_car_license_plates +
						    (random_key % invalid_array_size);
			} else {
				evt.type = MSG_CAMERA_EVT_TYPE_ERROR;
				evt.error_code = -EBUSY;
			}

			err = zbus_chan_pub(&chan_camera_evt, &evt, K_MSEC(200));
			if (err) {
				printk("Error code %d in %s (line:%d)\n", err, __FUNCTION__,
				       __LINE__);
				continue;
			}

		} break;
		default:
			printk("Command not suported\n");
			evt.type = MSG_CAMERA_EVT_TYPE_ERROR;
			evt.error_code = -ENOTSUP;
			err = zbus_chan_pub(&chan_camera_evt, &evt, K_MSEC(200));
			if (err) {
				printk("Error code %d in %s (line:%d)\n", err, __FUNCTION__,
				       __LINE__);
			}
		}
	}
}

K_THREAD_DEFINE(camera_thread_id, 2048, camera_thread, NULL, NULL, NULL, 3, 0, 0);
