/**
 * @file sys_event.h
 * @brief 模块控制消息和通知事件总线。
 */

#ifndef SYS_EVENT_H
#define SYS_EVENT_H

#include <stddef.h>
#include <stdint.h>

#include "sys_types.h"

#define SYS_EVENT_ABI_VERSION 1U

typedef struct {
	uint32_t id;
	uint32_t abi_version;
	const void *data;
	size_t size;
} sys_event_t;

typedef struct {
	uint64_t synchronous_publish_count;
	uint64_t asynchronous_publish_count;
	uint64_t callback_failure_count;
	uint64_t queue_full_count;
	size_t current_queue_depth;
	size_t queue_high_watermark;
} sys_event_stats_t;

typedef sys_err_t (*sys_event_callback_t)(const sys_event_t *event, void *private_data);

typedef struct sys_event_subscription sys_event_subscription_t;

/**
 * @brief 订阅指定事件。
 */
sys_err_t sys_event_subscribe(uint32_t event_id, sys_event_callback_t callback, void *private_data,
			      sys_event_subscription_t **out_subscription);

/**
 * @brief 取消订阅，并等待已经在途的回调完成。
 */
sys_err_t sys_event_unsubscribe(sys_event_subscription_t **subscription);

/**
 * @brief 在发布线程中同步调用订阅者，事件数据仅在调用期间有效。
 */
sys_err_t sys_event_publish_sync(uint32_t event_id, const void *data, size_t size);

/**
 * @brief 深拷贝事件数据并投递到有界异步队列。
 * @return 队列满时返回 SYS_ERR_QUEUE_FULL，不会静默丢弃。
 */
sys_err_t sys_event_publish_async(uint32_t event_id, const void *data, size_t size);

/** @brief 获取事件总线运行统计。 */
void sys_event_get_stats(sys_event_stats_t *out_stats);

#endif
