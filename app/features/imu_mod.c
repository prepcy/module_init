/**
 * @file imu_mod.c
 * @brief IMU 模块示例实现。
 */

#include "imu_mod.h"

static sys_err_t imu_real_get_acceleration(float *x, float *y, float *z)
{
	*x = 0.0F;
	*y = 0.0F;
	*z = 9.8F;
	return SYS_OK;
}

static const imu_ops_t g_imu_ops = {.get_acceleration = imu_real_get_acceleration};

static sys_err_t imu_init(void)
{
	const sys_service_desc_t service = {.module_id = SYS_MOD_IMU,
					    .interface_id = IMU_INTERFACE_CONTROL,
					    .abi_version = IMU_ABI_VERSION,
					    .ops_size = sizeof(g_imu_ops),
					    .ops = &g_imu_ops,
					    .name = "imu.control"};

	return sys_service_register(&service);
}

static void imu_exit(void)
{
	(void)sys_service_unregister(SYS_MOD_IMU, IMU_INTERFACE_CONTROL);
}

SYS_COMPONENT_REGISTER(g_imu_component, SYS_MOD_IMU, "imu", SYS_COMPONENT_PHASE_SERVICE, NULL, 0U, imu_init, imu_exit);
