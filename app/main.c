/**
 * @file main.c
 * @brief 框架控制面、事件面和大流量数据面示例。
 */

#include "camera_mod.h"
#include "gps_mod.h"
#include "imu_mod.h"
#include "wifi_mod.h"
#include "zlmedia_mod.h"

int main(void)
{
	int exit_code = 0;
	sys_runtime_t *runtime = NULL;
	sys_err_t ret = sys_runtime_create(&runtime);
	if (ret != SYS_OK) {
		SYS_LOG_ERROR_MSG("app", "runtime initialization failed: %s", sys_error_string(ret));
		return 1;
	}
	ret = sys_core_init();
	if (ret != SYS_OK) {
		SYS_LOG_ERROR_MSG("app", "framework initialization failed: %s", sys_error_string(ret));
		ret = sys_runtime_destroy(&runtime);
		if (ret != SYS_OK) {
			SYS_LOG_ERROR_MSG("app", "runtime rollback failed: %s", sys_error_string(ret));
		}
		return 1;
	}

	ret = wifi_connect("Mcu_Work_Wifi", "12345678");
	if (ret == SYS_OK) {
		int rssi;

		if (wifi_get_rssi(&rssi) == SYS_OK) {
			SYS_LOG_INFO_MSG("app", "WiFi RSSI: %d dBm", rssi);
		}
	} else {
		SYS_LOG_WARNING_MSG("app", "WiFi unavailable: %s", sys_error_string(ret));
	}

	double latitude;
	double longitude;
	ret = gps_get_coordinates(&latitude, &longitude);
	if (ret == SYS_OK) {
		SYS_LOG_INFO_MSG("app", "location: %.4f, %.4f", latitude, longitude);
	} else {
		SYS_LOG_WARNING_MSG("app", "GPS unavailable: %s", sys_error_string(ret));
	}

	float x;
	float y;
	float z;
	ret = imu_get_acceleration(&x, &y, &z);
	if (ret == SYS_OK) {
		SYS_LOG_INFO_MSG("app", "acceleration: %.1f, %.1f, %.1f", x, y, z);
	} else {
		SYS_LOG_WARNING_MSG("app", "IMU unavailable: %s", sys_error_string(ret));
	}

	ret = camera_start_stream(30U, 1920U, 1080U);
	if (ret == SYS_OK) {
		(void)zlmedia_set_port(8555U);
		ret = sys_runtime_wait(runtime, 500);
		if (ret != SYS_ERR_TIMEOUT && ret != SYS_ERR_CANCELLED) {
			SYS_LOG_ERROR_MSG("app", "runtime wait failed: %s", sys_error_string(ret));
			exit_code = 1;
		}
		ret = camera_stop_stream();
		if (ret != SYS_OK) {
			SYS_LOG_ERROR_MSG("app", "camera stop failed: %s", sys_error_string(ret));
			exit_code = 1;
		}
	} else {
		SYS_LOG_WARNING_MSG("app", "Camera unavailable: %s", sys_error_string(ret));
	}

	ret = wifi_disconnect();
	if (ret != SYS_OK && ret != SYS_ERR_NOT_FOUND) {
		SYS_LOG_WARNING_MSG("app", "WiFi disconnect failed: %s", sys_error_string(ret));
	}
	ret = sys_core_shutdown();
	if (ret != SYS_OK) {
		SYS_LOG_ERROR_MSG("app", "framework shutdown failed: %s", sys_error_string(ret));
		exit_code = 1;
	}
	ret = sys_runtime_destroy(&runtime);
	if (ret != SYS_OK) {
		SYS_LOG_ERROR_MSG("app", "runtime shutdown failed: %s", sys_error_string(ret));
		exit_code = 1;
	}
	return exit_code;
}
