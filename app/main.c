#include <stdio.h>
#include <unistd.h>
#include "wifi_mod.h"
#include "camera_mod.h"
#include "gps_mod.h"
#include "imu_mod.h"
#include "zlmedia_mod.h"

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
		printf("[Main业务] 提示：当前 WiFi 模块未启用，跳过网络业务。\n");
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
	 * 场景三：动态调用 GPS 和 IMU 模块
	 * =========================================================================
	 */
	double lat = 0.0, lng = 0.0;
	sys_err_t gps_ret = gps_get_coordinates(&lat, &lng);
	if (gps_ret != SYS_OK) {
		printf("[Main业务] 提示：当前 GPS 模块未启用，跳过定位业务。\n");
	}

	float ax = 0.0f, ay = 0.0f, az = 0.0f;
	sys_err_t imu_ret = imu_get_acceleration(&ax, &ay, &az);
	if (imu_ret != SYS_OK) {
		printf("[Main业务] 提示：当前 IMU 模块未启用，跳过惯导业务。\n");
	}

	/* =========================================================================
	 * 场景四：动态注销与生命周期自愈验证 (模拟 WiFi 模块热拔出)
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

	/* =========================================================================
	 * 场景五：模块间零拷贝高吞吐数据通道与 ioctl 控制验证 (Camera -> ZLMedia)
	 * =========================================================================
	 */
	printf("\n--- 启动模块间通信测试：Camera 开启采集，ZLMedia 消费流数据 ---\n");
	// 1. 通过统一 VFS ioctl 接口开启 Camera 视频流
	camera_start_streaming();

	// 2. 模拟运行 1.5 秒，观察拉流与帧消费情况 (Zero-copy payload print)
	usleep(1500000);

	// 3. 通过 ioctl 动态调整 ZLMedia 监听端口 (控制面解耦测试)
	int new_port = 8555;
	sys_subsystem_ioctl(SYS_MOD_ZLMEDIA, CMD_ZLM_SET_PORT, &new_port);

	// 4. 继续运行 1 秒
	usleep(1000000);

	// 5. 停止 Camera 视频流
	camera_stop_streaming();
	usleep(500000); // 等待收尾打印完成

	printf("\n--- 系统开始注销并关机... ---\n");
	do_exitcalls();

	return 0;
}
