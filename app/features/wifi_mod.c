/**
 * @file wifi_mod.c
 * @brief WiFi 模块示例实现。
 */

#include <stdio.h>

#include "wifi_mod.h"

static sys_err_t wifi_real_connect(const char *ssid, const char *password)
{
	(void)password;
	printf("[WiFi] connected to %s\n", ssid);
	return SYS_OK;
}

static sys_err_t wifi_real_get_rssi(int *out_rssi)
{
	*out_rssi = -45;
	return SYS_OK;
}

static sys_err_t wifi_real_disconnect(void)
{
	printf("[WiFi] disconnected\n");
	return SYS_OK;
}

static const wifi_ops_t g_wifi_ops = {
	.connect = wifi_real_connect, .get_rssi = wifi_real_get_rssi, .disconnect = wifi_real_disconnect};

static sys_err_t wifi_init(void)
{
	const sys_service_desc_t service = {.module_id = SYS_MOD_WIFI,
					    .interface_id = WIFI_INTERFACE_CONTROL,
					    .abi_version = WIFI_ABI_VERSION,
					    .ops_size = sizeof(g_wifi_ops),
					    .ops = &g_wifi_ops,
					    .name = "wifi.control"};

	return sys_service_register(&service);
}

static void wifi_exit(void)
{
	(void)sys_service_unregister(SYS_MOD_WIFI, WIFI_INTERFACE_CONTROL);
}

SYS_COMPONENT_REGISTER(g_wifi_component, SYS_MOD_WIFI, "wifi", SYS_COMPONENT_PHASE_SERVICE, NULL, 0U, wifi_init,
		       wifi_exit);
