#ifndef SYS_CORE_H
#define SYS_CORE_H

#include <stddef.h>

/* =========================================================================
 * 1. 自动分级初始化机制
 * ========================================================================= */
typedef int (*initcall_t)(void);

#define __app_init_export(fn, prio)                                            \
  static initcall_t __initcall_##fn                                            \
      __attribute__((used, section("app_init_prio" #prio "_sec"))) = fn

#define APP_INIT_PRIO_1(fn) __app_init_export(fn, 1)
#define APP_INIT_PRIO_2(fn) __app_init_export(fn, 2)
#define APP_INIT_PRIO_3(fn) __app_init_export(fn, 3)
 
void do_initcalls(void);

/* =========================================================================
 * 2. 运行时模块托管机制（使用通用 int 阻断依赖）
 * ========================================================================= */
// 定义核心能够容纳的最大模块槽位数
#define SYS_CORE_MAX_SLOTS 64

int sys_subsystem_register(int mod_id, void *ops);
void *sys_subsystem_get(int mod_id);

#endif // SYS_CORE_H
