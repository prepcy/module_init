#include "sys_core.h"
#include "app_modules.h"
#include "wifi_mod.h"
#include "camera_mod.h"
#include <stdio.h>

int main(void)
{
	// 1. 全自动分级初始化
	do_initcalls();

	printf("--- 应用层业务逻辑开始运转 ---\n\n");

	/* =========================================================================
	 * 场景一：调用千奇百怪的 WiFi 接口
	 * =========================================================================
	 */
	// 🌟 向框架索要 WiFi 操纵杆，并强转为它特有的业务类型
	wifi_ops_t *wifi = (wifi_ops_t *)sys_subsystem_get(SYS_MOD_WIFI);

	if (wifi != NULL) {
		// 模块存在，自由调用极其特异性的 API，保留完美的 C 语言类型检查！
		wifi->connect("Mcu_Work_Wifi", "12345678");
		printf("[Main业务] 当前网络信号强度: %d dBm\n", wifi->get_rssi());
	} else {
		// 🌟 即使 menuconfig 裁剪了 WiFi，这里也不会报错，应用层无感跳过
		printf("ℹ️  [Main业务] 提示：当前 WiFi 模块未启用，跳过网络业务。\n");
	}

	/* =========================================================================
	 * 场景二：调用完全不同的 Camera 接口
	 * =========================================================================
	 */
	// 🌟 向框架索要 Camera 操纵杆
	camera_ops_t *cam = (camera_ops_t *)sys_subsystem_get(SYS_MOD_CAMERA);

	if (cam != NULL) {
		// 模块存在，调用全然不同的奇特参数接口
		cam->start_stream(30, 1920, 1080);
	} else {
		printf("ℹ️  [Main业务] 提示：当前 Camera 模块未启用，跳过录像业务。\n");
	}

	return 0;
}
