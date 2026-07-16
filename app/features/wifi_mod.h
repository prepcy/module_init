#ifndef WIFI_MOD_H
#define WIFI_MOD_H

#include "sys_core.h"
#include "app_modules.h"

// WiFi 专有的特殊结构体
typedef struct {
	int (*connect)(const char *ssid, const char *pwd);
	int (*get_rssi)(void);
	void (*disconnect)(void);
} wifi_ops_t;

// WiFi 特有宏定义
#define WIFI_RSSI_INVALID (-127) // 无信号/未加载时的信号强度默认值

// 统一安全代理 API (内联展开，零开销防御段错误)
static inline sys_err_t wifi_connect(const char *ssid, const char *pwd)
{
	wifi_ops_t *ops = (wifi_ops_t *)sys_subsystem_get_lock(SYS_MOD_WIFI);
	if (!ops) {
		return SYS_ERR_NOT_FOUND;
	}
	if (!ops->connect) {
		sys_subsystem_put_lock(SYS_MOD_WIFI);
		return SYS_ERR_NOT_SUPPORTED;
	}
	int ret = ops->connect(ssid, pwd);
	sys_subsystem_put_lock(SYS_MOD_WIFI);
	return ret;
}

static inline int wifi_get_rssi(void)
{
	wifi_ops_t *ops = (wifi_ops_t *)sys_subsystem_get_lock(SYS_MOD_WIFI);
	if (!ops) {
		return WIFI_RSSI_INVALID;
	}
	int rssi = WIFI_RSSI_INVALID;
	if (ops->get_rssi) {
		rssi = ops->get_rssi();
	}
	sys_subsystem_put_lock(SYS_MOD_WIFI);
	return rssi;
}

static inline void wifi_disconnect(void)
{
	wifi_ops_t *ops = (wifi_ops_t *)sys_subsystem_get_lock(SYS_MOD_WIFI);
	if (ops) {
		if (ops->disconnect) {
			ops->disconnect();
		}
		sys_subsystem_put_lock(SYS_MOD_WIFI);
	}
}

#endif // WIFI_MOD_H
