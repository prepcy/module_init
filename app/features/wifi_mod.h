/**
 * @file wifi_mod.h
 * @brief WiFi 模块类型化控制接口。
 */

#ifndef WIFI_MOD_H
#define WIFI_MOD_H

#include "app_modules.h"
#include "sys_core.h"

#define WIFI_INTERFACE_CONTROL 1U
#define WIFI_ABI_VERSION 1U

typedef struct {
	sys_err_t (*connect)(const char *ssid, const char *password);
	sys_err_t (*get_rssi)(int *out_rssi);
	sys_err_t (*disconnect)(void);
} wifi_ops_t;

/** @brief 连接指定无线网络。 */
static inline sys_err_t wifi_connect(const char *ssid, const char *password)
{
	sys_service_ref_t ref;
	sys_err_t ret;

	if (ssid == NULL || password == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	ret = sys_service_acquire(SYS_MOD_WIFI, WIFI_INTERFACE_CONTROL, WIFI_ABI_VERSION, sizeof(wifi_ops_t), &ref);
	if (ret != SYS_OK) {
		return ret;
	}
	const wifi_ops_t *ops = ref.ops;
	ret = ops->connect == NULL ? SYS_ERR_NOT_SUPPORTED : ops->connect(ssid, password);
	sys_service_release(&ref);
	return ret;
}

/** @brief 获取当前信号强度。 */
static inline sys_err_t wifi_get_rssi(int *out_rssi)
{
	sys_service_ref_t ref;
	sys_err_t ret;

	if (out_rssi == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	ret = sys_service_acquire(SYS_MOD_WIFI, WIFI_INTERFACE_CONTROL, WIFI_ABI_VERSION, sizeof(wifi_ops_t), &ref);
	if (ret != SYS_OK) {
		return ret;
	}
	const wifi_ops_t *ops = ref.ops;
	ret = ops->get_rssi == NULL ? SYS_ERR_NOT_SUPPORTED : ops->get_rssi(out_rssi);
	sys_service_release(&ref);
	return ret;
}

/** @brief 断开无线网络。 */
static inline sys_err_t wifi_disconnect(void)
{
	sys_service_ref_t ref;
	sys_err_t ret;

	ret = sys_service_acquire(SYS_MOD_WIFI, WIFI_INTERFACE_CONTROL, WIFI_ABI_VERSION, sizeof(wifi_ops_t), &ref);
	if (ret != SYS_OK) {
		return ret;
	}
	const wifi_ops_t *ops = ref.ops;
	ret = ops->disconnect == NULL ? SYS_ERR_NOT_SUPPORTED : ops->disconnect();
	sys_service_release(&ref);
	return ret;
}

#endif
