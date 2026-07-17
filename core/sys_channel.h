/**
 * @file sys_channel.h
 * @brief 面向大流量数据传输的预分配缓冲池和有界零拷贝通道。
 */

#ifndef SYS_CHANNEL_H
#define SYS_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

#include "sys_types.h"

typedef struct sys_buffer_pool sys_buffer_pool_t;
typedef struct sys_buffer sys_buffer_t;
typedef struct sys_channel sys_channel_t;

typedef enum { SYS_CHANNEL_FULL_FAIL = 0, SYS_CHANNEL_FULL_DROP_OLDEST } sys_channel_full_policy_t;

typedef struct {
	size_t capacity;
	sys_channel_full_policy_t full_policy;
} sys_channel_config_t;

typedef struct {
	uint64_t sent_count;
	uint64_t received_count;
	uint64_t dropped_count;
	uint64_t full_count;
	uint64_t timeout_count;
	size_t current_depth;
	size_t high_watermark;
	size_t capacity;
} sys_channel_stats_t;

/**
 * @brief 创建固定块数量和容量的缓冲池。
 */
sys_err_t sys_buffer_pool_create(size_t block_count, size_t block_capacity, sys_buffer_pool_t **out_pool);

/**
 * @brief 销毁空闲缓冲池；仍有引用时返回 SYS_ERR_BUSY。
 */
sys_err_t sys_buffer_pool_destroy(sys_buffer_pool_t **pool);

/**
 * @brief 等待缓冲池中所有数据块归还。
 * @param timeout_ms 负数表示永久等待，0 表示只检查一次。
 */
sys_err_t sys_buffer_pool_wait_idle(sys_buffer_pool_t *pool, int timeout_ms);

/**
 * @brief 非阻塞获取一个空闲缓冲区，成功后调用者持有一个引用。
 */
sys_err_t sys_buffer_acquire(sys_buffer_pool_t *pool, sys_buffer_t **out_buffer);

/**
 * @brief 增加缓冲区引用。
 */
sys_err_t sys_buffer_retain(sys_buffer_t *buffer);

/**
 * @brief 释放缓冲区引用。
 */
void sys_buffer_release(sys_buffer_t *buffer);

/** @brief 获取 payload 可写地址。 */
void *sys_buffer_data(sys_buffer_t *buffer);

/** @brief 获取 payload 最大容量。 */
size_t sys_buffer_capacity(const sys_buffer_t *buffer);

/** @brief 获取当前有效数据长度。 */
size_t sys_buffer_size(const sys_buffer_t *buffer);

/** @brief 设置有效数据长度，长度不能超过容量。 */
sys_err_t sys_buffer_set_size(sys_buffer_t *buffer, size_t size);

/** @brief 获取缓冲区时间戳。 */
uint64_t sys_buffer_timestamp(const sys_buffer_t *buffer);

/** @brief 设置缓冲区时间戳。 */
void sys_buffer_set_timestamp(sys_buffer_t *buffer, uint64_t timestamp);

/** @brief 获取缓冲区序列号。 */
uint64_t sys_buffer_sequence(const sys_buffer_t *buffer);

/** @brief 设置缓冲区序列号。 */
void sys_buffer_set_sequence(sys_buffer_t *buffer, uint64_t sequence);

/** @brief 设置数据类型、格式版本和流代次。 */
void sys_buffer_set_contract(sys_buffer_t *buffer, uint32_t type, uint32_t version, uint64_t generation);

/** @brief 获取数据类型。 */
uint32_t sys_buffer_type(const sys_buffer_t *buffer);

/** @brief 获取数据格式版本。 */
uint32_t sys_buffer_version(const sys_buffer_t *buffer);

/** @brief 获取数据所属流代次。 */
uint64_t sys_buffer_generation(const sys_buffer_t *buffer);

/**
 * @brief 创建有界数据通道；创建者持有一个通道引用。
 */
sys_err_t sys_channel_create(size_t capacity, sys_channel_t **out_channel);

/** @brief 使用显式满队列策略创建数据通道。 */
sys_err_t sys_channel_create_with_config(const sys_channel_config_t *config, sys_channel_t **out_channel);

/**
 * @brief 增加通道所有权引用，适用于将通道交给另一个模块。
 */
sys_err_t sys_channel_retain(sys_channel_t *channel);

/**
 * @brief 释放通道所有权引用；最后一个引用负责销毁通道。
 */
void sys_channel_release(sys_channel_t *channel);

/**
 * @brief 关闭通道并唤醒所有等待者；已排队数据仍可继续读取。
 */
void sys_channel_close(sys_channel_t *channel);

/**
 * @brief 向通道发送缓冲区指针，通道会增加一个缓冲区引用。
 */
sys_err_t sys_channel_send(sys_channel_t *channel, sys_buffer_t *buffer, int timeout_ms);

/**
 * @brief 从通道接收缓冲区，成功后队列引用转移给调用者。
 */
sys_err_t sys_channel_receive(sys_channel_t *channel, sys_buffer_t **out_buffer, int timeout_ms);

/** @brief 获取当前排队缓冲区数量。 */
size_t sys_channel_count(sys_channel_t *channel);

/** @brief 获取通道运行统计。 */
sys_err_t sys_channel_get_stats(sys_channel_t *channel, sys_channel_stats_t *out_stats);

#endif
