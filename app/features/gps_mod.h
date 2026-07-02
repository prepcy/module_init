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
	gps_ops_t *ops = (gps_ops_t *)sys_subsystem_get(SYS_MOD_GPS);
	if (!ops) {
		return SYS_ERR_NOT_FOUND;
	}
	if (!ops->get_coordinates) {
		return SYS_ERR_NOT_SUPPORTED;
	}
	return ops->get_coordinates(latitude, longitude);
}

#endif // GPS_MOD_H
