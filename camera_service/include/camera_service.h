/**
 * @file
 * @brief Camera service
 *
 * The camera service works in a request reponse manner.
 */

#include <zephyr/zbus/zbus.h>

/**
 * @brief Start the camera capture.
 *
 * The capture data will be available at the chan_camera_evt channel as soons
 * as it is ready.
 *
 * @param timeout the time available for waiting the capture to start.
 * @return 0 if the capture started, negative otherwise.
 */
int camera_api_capture(k_timeout_t timeout);

struct camera_data {
	const char *plate;
	const char *hash;
};

struct msg_camera_evt {
	enum {
		MSG_CAMERA_EVT_TYPE_UNDEFINED,
		MSG_CAMERA_EVT_TYPE_DATA,
		MSG_CAMERA_EVT_TYPE_ERROR,
	} type;
	union {
		int error_code;
		const struct camera_data *captured_data;
	};
};

ZBUS_CHAN_DECLARE(chan_camera_evt); /* Message type: struct msg_camera_evt */
