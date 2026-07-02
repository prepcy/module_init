#include "sys_core.h"
#include <stdio.h>

extern initcall_t __start_app_init_prio1_sec[];
extern initcall_t __stop_app_init_prio1_sec[];
extern initcall_t __start_app_init_prio2_sec[];
extern initcall_t __stop_app_init_prio2_sec[];
extern initcall_t __start_app_init_prio3_sec[];
extern initcall_t __stop_app_init_prio3_sec[];

// 核心内部维护一个固定大小的万能指针数组，不再依赖外部的 SYS_MOD_MAX
static void *g_subsystem_registry[SYS_CORE_MAX_SLOTS] = {NULL};

static void execute_prio_level(initcall_t *start, initcall_t *stop) {
  for (initcall_t *call = start; call < stop; call++) {
    if (*call)
      (*call)();
  }
}

void do_initcalls(void) {
  execute_prio_level(__start_app_init_prio1_sec, __stop_app_init_prio1_sec);
  execute_prio_level(__start_app_init_prio2_sec, __stop_app_init_prio2_sec);
  execute_prio_level(__start_app_init_prio3_sec, __stop_app_init_prio3_sec);
}

int sys_subsystem_register(int mod_id, void *ops) {
  // 边界安全防御
  if (mod_id < 0 || mod_id >= SYS_CORE_MAX_SLOTS || !ops)
    return -1;
  g_subsystem_registry[mod_id] = ops;
  return 0;
}

void *sys_subsystem_get(int mod_id) {
  if (mod_id < 0 || mod_id >= SYS_CORE_MAX_SLOTS)
    return NULL;
  return g_subsystem_registry[mod_id];
}

/* 占位初始化函数，确保即便没有任何业务模块在该优先级注册，
 * 对应的 ELF 内存段也不会为空，从而保证 GCC 自动生成段边界符号 */
static int dummy_prio1_init(void) { return 0; }
static int dummy_prio2_init(void) { return 0; }
static int dummy_prio3_init(void) { return 0; }
APP_INIT_PRIO_1(dummy_prio1_init);
APP_INIT_PRIO_2(dummy_prio2_init);
APP_INIT_PRIO_3(dummy_prio3_init);

