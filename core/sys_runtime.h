/**
 * @file sys_runtime.h
 * @brief Linux进程信号和协作式退出运行时。
 */

#ifndef SYS_RUNTIME_H
#define SYS_RUNTIME_H

#include "sys_types.h"

typedef struct sys_runtime sys_runtime_t;

/** @brief 屏蔽SIGINT/SIGTERM并创建可轮询运行时。应在线程启动前调用。 */
sys_err_t sys_runtime_create(sys_runtime_t **out_runtime);

/** @brief 等待停止信号或程序化停止请求。 */
sys_err_t sys_runtime_wait(sys_runtime_t *runtime, int timeout_ms);

/** @brief 从任意普通线程请求应用退出。 */
sys_err_t sys_runtime_request_stop(sys_runtime_t *runtime);

/** @brief 关闭运行时并恢复创建线程的原信号掩码；必须由创建线程调用。 */
sys_err_t sys_runtime_destroy(sys_runtime_t **runtime);

#endif
