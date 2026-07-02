#include "imu_mod.h"
#include "sys_core.h"
#include "app_modules.h"
#include <stdio.h>

static int imu_real_get_acceleration(float *x, float *y, float *z)
{
	*x = 0.0f;
	*y = 0.0f;
	*z = 9.8f;
	printf("[IMU驱动] 读取加速度成功: X=%.1f, Y=%.1f, Z=%.1f m/s^2\n", *x, *y, *z);
	return 0;
}

// 组装 IMU 操作集
static const imu_ops_t my_real_imu_ops = {
	.get_acceleration = imu_real_get_acceleration
};

static int imu_subsys_init(void)
{
	// 注册 IMU 接口
	sys_subsystem_register(SYS_MOD_IMU, (void *)&my_real_imu_ops);
	return 0;
}
APP_INIT_PRIO_2(imu_subsys_init);
