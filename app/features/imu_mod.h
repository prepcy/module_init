/**
 * @file imu_mod.h
 * @brief IMU 模块类型化控制接口。
 */

#ifndef IMU_MOD_H
#define IMU_MOD_H

#include "app_modules.h"
#include "sys_core.h"

#define IMU_INTERFACE_CONTROL 1U
#define IMU_ABI_VERSION 1U

typedef struct {
	sys_err_t (*get_acceleration)(float *x, float *y, float *z);
} imu_ops_t;

/** @brief 获取三轴加速度。 */
static inline sys_err_t imu_get_acceleration(float *x, float *y, float *z)
{
	sys_service_ref_t ref;
	sys_err_t ret;

	if (x == NULL || y == NULL || z == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	ret = sys_service_acquire(SYS_MOD_IMU, IMU_INTERFACE_CONTROL, IMU_ABI_VERSION, sizeof(imu_ops_t), &ref);
	if (ret != SYS_OK) {
		return ret;
	}
	const imu_ops_t *ops = ref.ops;
	ret = ops->get_acceleration == NULL ? SYS_ERR_NOT_SUPPORTED : ops->get_acceleration(x, y, z);
	sys_service_release(&ref);
	return ret;
}

#endif
