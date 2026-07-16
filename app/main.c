/**
 * @file main.c
 * @brief 框架控制面、事件面和大流量数据面示例。
 */

#include <stdio.h>
#include <time.h>

#include "camera_mod.h"
#include "gps_mod.h"
#include "imu_mod.h"
#include "wifi_mod.h"
#include "zlmedia_mod.h"

static void sleep_milliseconds(unsigned int milliseconds)
{
	struct timespec delay = {.tv_sec = (time_t)(milliseconds / 1000U),
				 .tv_nsec = (long)(milliseconds % 1000U) * 1000000L};

	while (nanosleep(&delay, &delay) != 0) {
	}
}

int main(void)
{
	int exit_code = 0;
	sys_err_t ret = sys_core_init();
	if (ret != SYS_OK) {
		fprintf(stderr, "framework initialization failed: %d\n", ret);
		return 1;
	}

	ret = wifi_connect("Mcu_Work_Wifi", "12345678");
	if (ret == SYS_OK) {
		int rssi;

		if (wifi_get_rssi(&rssi) == SYS_OK) {
			printf("[App] WiFi RSSI: %d dBm\n", rssi);
		}
	} else {
		printf("[App] WiFi unavailable: %d\n", ret);
	}

	double latitude;
	double longitude;
	ret = gps_get_coordinates(&latitude, &longitude);
	if (ret == SYS_OK) {
		printf("[App] location: %.4f, %.4f\n", latitude, longitude);
	} else {
		printf("[App] GPS unavailable: %d\n", ret);
	}

	float x;
	float y;
	float z;
	ret = imu_get_acceleration(&x, &y, &z);
	if (ret == SYS_OK) {
		printf("[App] acceleration: %.1f, %.1f, %.1f\n", x, y, z);
	} else {
		printf("[App] IMU unavailable: %d\n", ret);
	}

	ret = camera_start_stream(30U, 1920U, 1080U);
	if (ret == SYS_OK) {
		(void)zlmedia_set_port(8555U);
		sleep_milliseconds(500U);
		ret = camera_stop_stream();
		if (ret != SYS_OK) {
			fprintf(stderr, "camera stop failed: %d\n", ret);
			exit_code = 1;
		}
	} else {
		printf("[App] Camera unavailable: %d\n", ret);
	}

	(void)wifi_disconnect();
	sys_core_shutdown();
	return exit_code;
}
