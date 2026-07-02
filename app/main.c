#include "wifi_mod.h"
#include "camera_mod.h"
#include <stdio.h>

int main(void)
{
	// 1. 全自动分级初始化
	do_initcalls();

	printf("--- 应用层业务逻辑开始运转 ---\n\n");

	/* =========================================================================
	 * 场景一：调用千奇百怪的 WiFi 接口（通过安全代理 API 直接调用）
	 * =========================================================================
	 */
	sys_err_t wifi_ret = wifi_connect("Mcu_Work_Wifi", "12345678");
	if (wifi_ret == SYS_OK) {
		printf("[Main业务] 当前网络信号强度: %d dBm\n", wifi_get_rssi());
	} else {
		printf("ℹ️  [Main业务] 提示：当前 WiFi 模块未启用，跳过网络业务。\n");
	}

	/* =========================================================================
	 * 场景二：调用完全不同的 Camera 接口（通过安全代理 API 直接调用）
	 * =========================================================================
	 */
	sys_err_t cam_ret = camera_start_stream(30, 1920, 1080);
	if (cam_ret != SYS_OK) {
		printf("[Main业务] 提示：当前 Camera 模块未启用，跳过录像业务。\n");
	}

	/* =========================================================================
	 * 场景三：动态注销与生命周期自愈验证 (模拟 WiFi 模块热拔出)
	 * =========================================================================
	 */
	printf("\n--- 模拟运行时 WiFi 模块热拔出... ---\n");
	sys_err_t unreg_ret = sys_subsystem_unregister(SYS_MOD_WIFI);
	if (unreg_ret == SYS_OK) {
		printf("[Main业务] WiFi 模块注销成功！\n");
	}

	printf("--- 再次尝试调用 WiFi 接口... ---\n");
	wifi_ret = wifi_connect("Mcu_Work_Wifi", "12345678");
	if (wifi_ret == SYS_OK) {
		printf("[Main业务] 当前网络信号强度: %d dBm\n", wifi_get_rssi());
	} else {
		// 期待运行至此，表示注销后代理函数安全平滑降维，未崩溃
		printf("[Main业务] 提示：WiFi 模块已被卸载，自动跳过网络同步。\n");
	}

	return 0;
}
