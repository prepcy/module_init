#ifndef IMU_MOD_H
#define IMU_MOD_H

#include "sys_core.h"
#include "app_modules.h"

// IMU 专属操作虚表
typedef struct {
	int (*get_acceleration)(float *x, float *y, float *z);
} imu_ops_t;

// 统一安全代理 API (内联展开，零开销防御段错误)
static inline sys_err_t imu_get_acceleration(float *x, float *y, float *z)
{
	imu_ops_t *ops = (imu_ops_t *)sys_subsystem_get_lock(SYS_MOD_IMU);
	if (!ops) {
		return SYS_ERR_NOT_FOUND;
	}
	if (!ops->get_acceleration) {
		sys_subsystem_put_lock(SYS_MOD_IMU);
		return SYS_ERR_NOT_SUPPORTED;
	}
	int ret = ops->get_acceleration(x, y, z);
	sys_subsystem_put_lock(SYS_MOD_IMU);
	return ret;
}

#endif // IMU_MOD_H
