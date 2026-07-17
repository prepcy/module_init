/**
 * @file sys_core.h
 * @brief Linux C 应用框架公共入口。
 */

#ifndef SYS_CORE_H
#define SYS_CORE_H

#include <stdbool.h>

#include "sys_channel.h"
#include "sys_component.h"
#include "sys_event.h"
#include "sys_log.h"
#include "sys_runtime.h"
#include "sys_service.h"
#include "sys_thread.h"
#include "sys_types.h"
#include "sys_version.h"

typedef struct {
	bool running;
	size_t component_count;
	size_t running_component_count;
	size_t failed_component_count;
	size_t service_count;
	size_t active_thread_count;
	size_t unjoined_thread_count;
	sys_event_stats_t event_stats;
} sys_core_status_t;

/**
 * @brief 初始化框架基础设施并按依赖顺序启动所有组件。
 * @return SYS_OK 表示成功，否则返回第一个失败原因。
 */
sys_err_t sys_core_init(void);

/**
 * @brief 逆序停止已启动组件并关闭框架基础设施。
 * @return SYS_OK 表示完整关闭，否则返回第一个关闭错误。
 */
sys_err_t sys_core_shutdown(void);

/**
 * @brief 查询框架是否已经完成初始化。
 * @return true 表示框架正在运行。
 */
bool sys_core_is_running(void);

/** @brief 获取框架整体运行状态。 */
void sys_core_get_status(sys_core_status_t *out_status);

#endif
