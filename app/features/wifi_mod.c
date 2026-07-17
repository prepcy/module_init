/**
 * @file wifi_mod.c
 * @brief WiFi 模块示例实现。
 */

#include <stdatomic.h>

#include "wifi_mod.h"

static atomic_bool g_wifi_connected;

static sys_err_t wifi_real_connect(const char *ssid, const char *password)
{
	(void)password;
	atomic_store_explicit(&g_wifi_connected, true, memory_order_release);
	SYS_LOG_INFO_MSG("wifi", "connected to %s", ssid);
	return SYS_OK;
}

static sys_err_t wifi_real_get_rssi(int *out_rssi)
{
	if (!atomic_load_explicit(&g_wifi_connected, memory_order_acquire)) {
		return SYS_ERR_STATE;
	}
	*out_rssi = -45;
	return SYS_OK;
}

static sys_err_t wifi_real_disconnect(void)
{
	if (atomic_exchange_explicit(&g_wifi_connected, false, memory_order_acq_rel)) {
		SYS_LOG_INFO_MSG("wifi", "disconnected");
	}
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

	atomic_init(&g_wifi_connected, false);
	return sys_service_register(&service);
}

static sys_err_t wifi_stop(void)
{
	return wifi_real_disconnect();
}

static void wifi_deinit(void)
{
	sys_err_t ret = sys_service_unregister(SYS_MOD_WIFI, WIFI_INTERFACE_CONTROL);

	if (ret != SYS_OK) {
		SYS_LOG_ERROR_MSG("wifi", "service unregister failed: %s", sys_error_string(ret));
	}
}

SYS_COMPONENT_REGISTER(g_wifi_component, .id = SYS_MOD_WIFI, .name = "wifi", .phase = SYS_COMPONENT_PHASE_SERVICE,
		       .policy = SYS_COMPONENT_OPTIONAL, .init = wifi_init, .stop = wifi_stop, .deinit = wifi_deinit);
