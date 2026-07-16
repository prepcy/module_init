#ifndef GPS_MOD_H
#define GPS_MOD_H

#include "sys_core.h"
#include "app_modules.h"

// GPS 专属操作虚表
typedef struct {
	int (*get_coordinates)(double *latitude, double *longitude);
} gps_ops_t;

// 统一安全代理 API (内联展开，零开销防御段错误)
static inline sys_err_t gps_get_coordinates(double *latitude, double *longitude)
{
	gps_ops_t *ops = (gps_ops_t *)sys_subsystem_get_lock(SYS_MOD_GPS);
	if (!ops) {
		return SYS_ERR_NOT_FOUND;
	}
	if (!ops->get_coordinates) {
		sys_subsystem_put_lock(SYS_MOD_GPS);
		return SYS_ERR_NOT_SUPPORTED;
	}
	int ret = ops->get_coordinates(latitude, longitude);
	sys_subsystem_put_lock(SYS_MOD_GPS);
	return ret;
}

#endif // GPS_MOD_H
