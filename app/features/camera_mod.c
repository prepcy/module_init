#include "camera_mod.h"
#include "sys_core.h"
#include "app_modules.h"
#include <stdio.h>

static int cam_real_start(int fps, int w, int h)
{
	printf("[Camera驱动] 开启视频流成功！参数: %d FPS, 分辨率: %dx%d\n", fps, w, h);
	return 0;
}

static const camera_ops_t my_real_cam_ops = { .start_stream = cam_real_start,
					      .get_frame_buffer = NULL,
					      .stop_stream = NULL };

static int camera_subsys_init(void)
{
	// 将自己独特的操纵杆登记到 Camera 模块槽位
	printf("[Camera驱动] 正在自加载过程...\n");
	sys_subsystem_register(SYS_MOD_CAMERA, (void *)&my_real_cam_ops);
	return 0;
}

static void camera_subsys_exit(void)
{
	printf("[Camera驱动] 正在注销释放过程...\n");
	sys_subsystem_unregister(SYS_MOD_CAMERA);
}
APP_REGISTER(camera_subsys_init, camera_subsys_exit, SYS_MOD_CAMERA); // 自动开机关机生命周期托管
