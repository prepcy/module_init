#include "gps_mod.h"
#include "sys_core.h"
#include "app_modules.h"
#include <stdio.h>

static int gps_real_get_coordinates(double *lat, double *lng)
{
	*lat = 39.9042;
	*lng = 116.4074;
	printf("[GPS驱动] 获取经纬度成功: 纬度 %.4f, 经度 %.4f\n", *lat, *lng);
	return 0;
}

// 组装 GPS 操作集
static const gps_ops_t my_real_gps_ops = { .get_coordinates = gps_real_get_coordinates };

static int gps_subsys_init(void)
{
	// 注册 GPS 接口
	printf("[GPS驱动] 正在自加载过程...\n");
	sys_subsystem_register(SYS_MOD_GPS, (void *)&my_real_gps_ops);
	return 0;
}
APP_INIT_REGISTER(gps_subsys_init, SYS_MOD_GPS); // 自动按序分级初始化
