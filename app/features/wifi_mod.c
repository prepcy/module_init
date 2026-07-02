#include "wifi_mod.h"
#include "sys_core.h"
#include "app_modules.h"
#include <stdio.h>

static int wifi_real_connect(const char *ssid, const char *pwd)
{
	(void)pwd; // 抑制未使用参数警告
	printf("[WiFi驱动] 正在连接路由器: %s ... 成功！\n", ssid);
	return 0;
}

static int wifi_real_get_rssi(void)
{
	return -45;
}
static void wifi_real_disconnect(void)
{
	printf("[WiFi驱动] 已断开连接。\n");
}

// 组装专属的操作集
static const wifi_ops_t my_real_wifi_ops = { .connect = wifi_real_connect,
					     .get_rssi = wifi_real_get_rssi,
					     .disconnect = wifi_real_disconnect };

static int wifi_subsys_init(void)
{
	// 将自己独特的操纵杆登记到 WiFi 模块槽位
	sys_subsystem_register(SYS_MOD_WIFI, (void *)&my_real_wifi_ops);

	return 0;
}
APP_INIT_PRIO_2(wifi_subsys_init); // 自动分级初始化
