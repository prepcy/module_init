/**
 * @file sys_thread.h
 * @brief Linux应用线程命名、停止请求和回收管理。
 */

#ifndef SYS_THREAD_H
#define SYS_THREAD_H

#include <stdbool.h>
#include <stddef.h>

#include "sys_types.h"

typedef struct sys_thread sys_thread_t;
typedef void *(*sys_thread_entry_t)(sys_thread_t *thread, void *argument);

/** @brief 创建一个可管理线程。 */
sys_err_t sys_thread_create(const char *name, sys_thread_entry_t entry, void *argument, sys_thread_t **out_thread);

/** @brief 向线程发送协作式停止请求。 */
void sys_thread_request_stop(sys_thread_t *thread);

/** @brief 在线程入口中查询停止请求。 */
bool sys_thread_stop_requested(const sys_thread_t *thread);

/** @brief 等待线程结束并释放句柄；负超时表示永久等待。 */
sys_err_t sys_thread_join(sys_thread_t **thread, int timeout_ms);

/** @brief 获取当前仍在运行的受管线程数量。 */
size_t sys_thread_active_count(void);

/** @brief 获取尚未 join 并释放句柄的受管线程数量。 */
size_t sys_thread_unjoined_count(void);

#endif
