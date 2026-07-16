/**
 * @file gps_mod.c
 * @brief GPS 模块示例实现。
 */

#include "gps_mod.h"

static sys_err_t gps_real_get_coordinates(double *latitude, double *longitude)
{
	*latitude = 39.9042;
	*longitude = 116.4074;
	return SYS_OK;
}

static const gps_ops_t g_gps_ops = {.get_coordinates = gps_real_get_coordinates};

static sys_err_t gps_init(void)
{
	const sys_service_desc_t service = {.module_id = SYS_MOD_GPS,
					    .interface_id = GPS_INTERFACE_CONTROL,
					    .abi_version = GPS_ABI_VERSION,
					    .ops_size = sizeof(g_gps_ops),
					    .ops = &g_gps_ops,
					    .name = "gps.control"};

	return sys_service_register(&service);
}

static void gps_exit(void)
{
	(void)sys_service_unregister(SYS_MOD_GPS, GPS_INTERFACE_CONTROL);
}

SYS_COMPONENT_REGISTER(g_gps_component, SYS_MOD_GPS, "gps", SYS_COMPONENT_PHASE_SERVICE, NULL, 0U, gps_init, gps_exit);
