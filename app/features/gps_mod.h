/**
 * @file gps_mod.h
 * @brief GPS 模块类型化控制接口。
 */

#ifndef GPS_MOD_H
#define GPS_MOD_H

#include "app_modules.h"
#include "sys_core.h"

#define GPS_INTERFACE_CONTROL 1U
#define GPS_ABI_VERSION 1U

typedef struct {
	sys_err_t (*get_coordinates)(double *latitude, double *longitude);
} gps_ops_t;

/** @brief 获取当前经纬度。 */
static inline sys_err_t gps_get_coordinates(double *latitude, double *longitude)
{
	sys_service_ref_t ref;
	sys_err_t ret;

	if (latitude == NULL || longitude == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	ret = sys_service_acquire(SYS_MOD_GPS, GPS_INTERFACE_CONTROL, GPS_ABI_VERSION, sizeof(gps_ops_t), &ref);
	if (ret != SYS_OK) {
		return ret;
	}
	const gps_ops_t *ops = ref.ops;
	ret = ops->get_coordinates == NULL ? SYS_ERR_NOT_SUPPORTED : ops->get_coordinates(latitude, longitude);
	sys_service_release(&ref);
	return ret;
}

#endif
