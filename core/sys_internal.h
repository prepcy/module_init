/**
 * @file sys_internal.h
 * @brief 核心子系统之间使用的内部接口。
 */

#ifndef SYS_INTERNAL_H
#define SYS_INTERNAL_H

#include "sys_types.h"

sys_err_t sys_component_start_all(void);
sys_err_t sys_component_stop_all(void);

sys_err_t sys_event_bus_start(void);
sys_err_t sys_event_bus_stop(void);

void sys_service_registry_reset(void);

#endif
