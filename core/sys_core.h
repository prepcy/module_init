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
#include "sys_service.h"
#include "sys_types.h"

/**
 * @brief 初始化框架基础设施并按依赖顺序启动所有组件。
 * @return SYS_OK 表示成功，否则返回第一个失败原因。
 */
sys_err_t sys_core_init(void);

/**
 * @brief 逆序停止已启动组件并关闭框架基础设施。
 */
void sys_core_shutdown(void);

/**
 * @brief 查询框架是否已经完成初始化。
 * @return true 表示框架正在运行。
 */
bool sys_core_is_running(void);

#endif
