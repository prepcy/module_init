/**
 * @file sys_channel.c
 * @brief 预分配缓冲池和有界数据通道实现。
 */

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sys_channel.h"

struct sys_buffer {
	sys_buffer_pool_t *pool;
	unsigned char *data;
	size_t capacity;
	size_t size;
	uint64_t timestamp;
	uint64_t sequence;
	uint64_t generation;
	uint32_t type;
	uint32_t version;
	size_t reference_count;
};

struct sys_buffer_pool {
	pthread_mutex_t mutex;
	pthread_cond_t idle_cond;
	sys_buffer_t *buffers;
	unsigned char *storage;
	size_t block_count;
	size_t block_capacity;
	size_t buffers_in_use;
};

struct sys_channel {
	pthread_mutex_t mutex;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
	sys_buffer_t **queue;
	size_t capacity;
	size_t head;
	size_t count;
	bool closed;
	sys_channel_full_policy_t full_policy;
	sys_channel_stats_t stats;
	atomic_uint reference_count;
};

static void make_deadline(struct timespec *deadline, int timeout_ms)
{
	clock_gettime(CLOCK_MONOTONIC, deadline);
	deadline->tv_sec += timeout_ms / 1000;
	deadline->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
	if (deadline->tv_nsec >= 1000000000L) {
		deadline->tv_sec++;
		deadline->tv_nsec -= 1000000000L;
	}
}

static int init_monotonic_cond(pthread_cond_t *condition)
{
	pthread_condattr_t attributes;
	int ret;

	ret = pthread_condattr_init(&attributes);
	if (ret != 0) {
		return ret;
	}
	ret = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
	if (ret == 0) {
		ret = pthread_cond_init(condition, &attributes);
	}
	pthread_condattr_destroy(&attributes);
	return ret;
}

sys_err_t sys_buffer_pool_create(size_t block_count, size_t block_capacity, sys_buffer_pool_t **out_pool)
{
	sys_buffer_pool_t *pool;

	if (out_pool == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	*out_pool = NULL;
	if (block_count == 0U || block_capacity == 0U || block_count > SIZE_MAX / block_capacity ||
	    block_count > SIZE_MAX / sizeof(sys_buffer_t)) {
		return SYS_ERR_INVALID_PARAM;
	}
	pool = calloc(1U, sizeof(*pool));
	if (pool == NULL) {
		return SYS_ERR_NO_MEMORY;
	}
	pool->buffers = calloc(block_count, sizeof(*pool->buffers));
	pool->storage = malloc(block_count * block_capacity);
	if (pool->buffers == NULL || pool->storage == NULL) {
		free(pool->storage);
		free(pool->buffers);
		free(pool);
		return SYS_ERR_NO_MEMORY;
	}
	if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
		free(pool->storage);
		free(pool->buffers);
		free(pool);
		return SYS_ERR_GENERIC;
	}
	if (init_monotonic_cond(&pool->idle_cond) != 0) {
		pthread_mutex_destroy(&pool->mutex);
		free(pool->storage);
		free(pool->buffers);
		free(pool);
		return SYS_ERR_GENERIC;
	}

	pool->block_count = block_count;
	pool->block_capacity = block_capacity;
	for (size_t i = 0; i < block_count; i++) {
		pool->buffers[i].pool = pool;
		pool->buffers[i].data = pool->storage + i * block_capacity;
		pool->buffers[i].capacity = block_capacity;
	}
	*out_pool = pool;
	return SYS_OK;
}

sys_err_t sys_buffer_pool_destroy(sys_buffer_pool_t **pool_ptr)
{
	sys_buffer_pool_t *pool;

	if (pool_ptr == NULL || *pool_ptr == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	pool = *pool_ptr;
	pthread_mutex_lock(&pool->mutex);
	if (pool->buffers_in_use != 0U) {
		pthread_mutex_unlock(&pool->mutex);
		return SYS_ERR_BUSY;
	}
	pthread_mutex_unlock(&pool->mutex);

	pthread_cond_destroy(&pool->idle_cond);
	pthread_mutex_destroy(&pool->mutex);
	free(pool->storage);
	free(pool->buffers);
	free(pool);
	*pool_ptr = NULL;
	return SYS_OK;
}

sys_err_t sys_buffer_pool_wait_idle(sys_buffer_pool_t *pool, int timeout_ms)
{
	int wait_ret = 0;
	struct timespec deadline;

	if (pool == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	if (timeout_ms > 0) {
		make_deadline(&deadline, timeout_ms);
	}

	pthread_mutex_lock(&pool->mutex);
	while (pool->buffers_in_use != 0U) {
		if (timeout_ms == 0) {
			pthread_mutex_unlock(&pool->mutex);
			return SYS_ERR_BUSY;
		}
		if (timeout_ms < 0) {
			wait_ret = pthread_cond_wait(&pool->idle_cond, &pool->mutex);
		} else {
			wait_ret = pthread_cond_timedwait(&pool->idle_cond, &pool->mutex, &deadline);
		}
		if (wait_ret != 0) {
			pthread_mutex_unlock(&pool->mutex);
			return wait_ret == ETIMEDOUT ? SYS_ERR_TIMEOUT : sys_error_from_errno(wait_ret);
		}
	}
	pthread_mutex_unlock(&pool->mutex);
	return SYS_OK;
}

sys_err_t sys_buffer_acquire(sys_buffer_pool_t *pool, sys_buffer_t **out_buffer)
{
	if (out_buffer == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	*out_buffer = NULL;
	if (pool == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	pthread_mutex_lock(&pool->mutex);
	for (size_t i = 0; i < pool->block_count; i++) {
		sys_buffer_t *buffer = &pool->buffers[i];

		if (buffer->reference_count == 0U) {
			buffer->reference_count = 1U;
			buffer->size = 0U;
			buffer->timestamp = 0U;
			buffer->sequence = 0U;
			buffer->generation = 0U;
			buffer->type = 0U;
			buffer->version = 0U;
			pool->buffers_in_use++;
			*out_buffer = buffer;
			pthread_mutex_unlock(&pool->mutex);
			return SYS_OK;
		}
	}
	pthread_mutex_unlock(&pool->mutex);
	return SYS_ERR_BUSY;
}

sys_err_t sys_buffer_retain(sys_buffer_t *buffer)
{
	if (buffer == NULL || buffer->pool == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	pthread_mutex_lock(&buffer->pool->mutex);
	if (buffer->reference_count == 0U) {
		pthread_mutex_unlock(&buffer->pool->mutex);
		return SYS_ERR_STATE;
	}
	if (buffer->reference_count == SIZE_MAX) {
		pthread_mutex_unlock(&buffer->pool->mutex);
		return SYS_ERR_OVERFLOW;
	}
	buffer->reference_count++;
	pthread_mutex_unlock(&buffer->pool->mutex);
	return SYS_OK;
}

void sys_buffer_release(sys_buffer_t *buffer)
{
	sys_buffer_pool_t *pool;

	if (buffer == NULL || buffer->pool == NULL) {
		return;
	}
	pool = buffer->pool;
	pthread_mutex_lock(&pool->mutex);
	if (buffer->reference_count > 0U) {
		buffer->reference_count--;
		if (buffer->reference_count == 0U) {
			pool->buffers_in_use--;
			pthread_cond_broadcast(&pool->idle_cond);
		}
	}
	pthread_mutex_unlock(&pool->mutex);
}

void *sys_buffer_data(sys_buffer_t *buffer)
{
	return buffer == NULL ? NULL : buffer->data;
}

size_t sys_buffer_capacity(const sys_buffer_t *buffer)
{
	return buffer == NULL ? 0U : buffer->capacity;
}

size_t sys_buffer_size(const sys_buffer_t *buffer)
{
	return buffer == NULL ? 0U : buffer->size;
}

sys_err_t sys_buffer_set_size(sys_buffer_t *buffer, size_t size)
{
	if (buffer == NULL || size > buffer->capacity) {
		return SYS_ERR_INVALID_PARAM;
	}
	buffer->size = size;
	return SYS_OK;
}

uint64_t sys_buffer_timestamp(const sys_buffer_t *buffer)
{
	return buffer == NULL ? 0U : buffer->timestamp;
}

void sys_buffer_set_timestamp(sys_buffer_t *buffer, uint64_t timestamp)
{
	if (buffer != NULL) {
		buffer->timestamp = timestamp;
	}
}

uint64_t sys_buffer_sequence(const sys_buffer_t *buffer)
{
	return buffer == NULL ? 0U : buffer->sequence;
}

void sys_buffer_set_sequence(sys_buffer_t *buffer, uint64_t sequence)
{
	if (buffer != NULL) {
		buffer->sequence = sequence;
	}
}

void sys_buffer_set_contract(sys_buffer_t *buffer, uint32_t type, uint32_t version, uint64_t generation)
{
	if (buffer != NULL) {
		buffer->type = type;
		buffer->version = version;
		buffer->generation = generation;
	}
}

uint32_t sys_buffer_type(const sys_buffer_t *buffer)
{
	return buffer == NULL ? 0U : buffer->type;
}

uint32_t sys_buffer_version(const sys_buffer_t *buffer)
{
	return buffer == NULL ? 0U : buffer->version;
}

uint64_t sys_buffer_generation(const sys_buffer_t *buffer)
{
	return buffer == NULL ? 0U : buffer->generation;
}

sys_err_t sys_channel_create(size_t capacity, sys_channel_t **out_channel)
{
	const sys_channel_config_t config = {.capacity = capacity, .full_policy = SYS_CHANNEL_FULL_FAIL};
	return sys_channel_create_with_config(&config, out_channel);
}

sys_err_t sys_channel_create_with_config(const sys_channel_config_t *config, sys_channel_t **out_channel)
{
	sys_channel_t *channel;

	if (out_channel == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	*out_channel = NULL;
	if (config == NULL || config->capacity == 0U || config->capacity > SIZE_MAX / sizeof(sys_buffer_t *) ||
	    config->full_policy > SYS_CHANNEL_FULL_DROP_OLDEST) {
		return SYS_ERR_INVALID_PARAM;
	}
	channel = calloc(1U, sizeof(*channel));
	if (channel == NULL) {
		return SYS_ERR_NO_MEMORY;
	}
	channel->queue = calloc(config->capacity, sizeof(sys_buffer_t *));
	if (channel->queue == NULL) {
		free(channel);
		return SYS_ERR_NO_MEMORY;
	}
	if (pthread_mutex_init(&channel->mutex, NULL) != 0) {
		free(channel->queue);
		free(channel);
		return SYS_ERR_GENERIC;
	}
	if (init_monotonic_cond(&channel->not_empty) != 0) {
		pthread_mutex_destroy(&channel->mutex);
		free(channel->queue);
		free(channel);
		return SYS_ERR_GENERIC;
	}
	if (init_monotonic_cond(&channel->not_full) != 0) {
		pthread_cond_destroy(&channel->not_empty);
		pthread_mutex_destroy(&channel->mutex);
		free(channel->queue);
		free(channel);
		return SYS_ERR_GENERIC;
	}
	channel->capacity = config->capacity;
	channel->full_policy = config->full_policy;
	channel->stats.capacity = config->capacity;
	atomic_init(&channel->reference_count, 1U);
	*out_channel = channel;
	return SYS_OK;
}

sys_err_t sys_channel_retain(sys_channel_t *channel)
{
	unsigned int references;

	if (channel == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	references = atomic_load_explicit(&channel->reference_count, memory_order_relaxed);
	while (references != 0U) {
		if (references == UINT_MAX) {
			return SYS_ERR_OVERFLOW;
		}
		if (atomic_compare_exchange_weak_explicit(&channel->reference_count, &references, references + 1U,
							  memory_order_acquire, memory_order_relaxed)) {
			return SYS_OK;
		}
	}
	return SYS_ERR_CLOSED;
}

void sys_channel_release(sys_channel_t *channel)
{
	if (channel == NULL) {
		return;
	}
	if (atomic_fetch_sub_explicit(&channel->reference_count, 1U, memory_order_acq_rel) != 1U) {
		return;
	}

	for (size_t i = 0; i < channel->count; i++) {
		size_t index = (channel->head + i) % channel->capacity;

		sys_buffer_release(channel->queue[index]);
	}
	channel->count = 0U;
	pthread_cond_destroy(&channel->not_empty);
	pthread_cond_destroy(&channel->not_full);
	pthread_mutex_destroy(&channel->mutex);
	free(channel->queue);
	free(channel);
}

void sys_channel_close(sys_channel_t *channel)
{
	if (channel == NULL) {
		return;
	}
	pthread_mutex_lock(&channel->mutex);
	channel->closed = true;
	pthread_cond_broadcast(&channel->not_empty);
	pthread_cond_broadcast(&channel->not_full);
	pthread_mutex_unlock(&channel->mutex);
}

static sys_err_t wait_for_channel_state(pthread_cond_t *condition, pthread_mutex_t *mutex, int timeout_ms,
					const struct timespec *deadline, sys_err_t immediate_error)
{
	int ret;

	if (timeout_ms == 0) {
		return immediate_error;
	}
	if (timeout_ms < 0) {
		ret = pthread_cond_wait(condition, mutex);
	} else {
		ret = pthread_cond_timedwait(condition, mutex, deadline);
	}
	if (ret == 0) {
		return SYS_OK;
	}
	return ret == ETIMEDOUT ? SYS_ERR_TIMEOUT : sys_error_from_errno(ret);
}

sys_err_t sys_channel_send(sys_channel_t *channel, sys_buffer_t *buffer, int timeout_ms)
{
	struct timespec deadline = {0};
	sys_buffer_t *dropped = NULL;
	sys_err_t ret;
	bool full_observed = false;

	if (channel == NULL || buffer == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	if (timeout_ms > 0) {
		make_deadline(&deadline, timeout_ms);
	}
	ret = sys_buffer_retain(buffer);
	if (ret != SYS_OK) {
		return ret;
	}

	pthread_mutex_lock(&channel->mutex);
	if (channel->count == channel->capacity) {
		channel->stats.full_count++;
		full_observed = true;
	}
	if (full_observed && channel->full_policy == SYS_CHANNEL_FULL_DROP_OLDEST && !channel->closed) {
		dropped = channel->queue[channel->head];
		channel->queue[channel->head] = NULL;
		channel->head = (channel->head + 1U) % channel->capacity;
		channel->count--;
		channel->stats.dropped_count++;
	}
	while (channel->count == channel->capacity && !channel->closed) {
		ret = wait_for_channel_state(&channel->not_full, &channel->mutex, timeout_ms, &deadline,
					     SYS_ERR_QUEUE_FULL);
		if (ret != SYS_OK) {
			if (ret == SYS_ERR_TIMEOUT) {
				channel->stats.timeout_count++;
			}
			pthread_mutex_unlock(&channel->mutex);
			sys_buffer_release(buffer);
			return ret;
		}
	}
	if (channel->closed) {
		pthread_mutex_unlock(&channel->mutex);
		sys_buffer_release(buffer);
		if (dropped != NULL) {
			sys_buffer_release(dropped);
		}
		return SYS_ERR_CLOSED;
	}

	size_t index = (channel->head + channel->count) % channel->capacity;
	channel->queue[index] = buffer;
	channel->count++;
	channel->stats.sent_count++;
	if (channel->count > channel->stats.high_watermark) {
		channel->stats.high_watermark = channel->count;
	}
	pthread_cond_signal(&channel->not_empty);
	pthread_mutex_unlock(&channel->mutex);
	if (dropped != NULL) {
		sys_buffer_release(dropped);
	}
	return SYS_OK;
}

sys_err_t sys_channel_receive(sys_channel_t *channel, sys_buffer_t **out_buffer, int timeout_ms)
{
	struct timespec deadline = {0};
	sys_err_t ret;

	if (channel == NULL || out_buffer == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	*out_buffer = NULL;
	if (timeout_ms > 0) {
		make_deadline(&deadline, timeout_ms);
	}

	pthread_mutex_lock(&channel->mutex);
	while (channel->count == 0U && !channel->closed) {
		ret = wait_for_channel_state(&channel->not_empty, &channel->mutex, timeout_ms, &deadline,
					     SYS_ERR_TIMEOUT);
		if (ret != SYS_OK) {
			channel->stats.timeout_count++;
			pthread_mutex_unlock(&channel->mutex);
			return ret;
		}
	}
	if (channel->count == 0U && channel->closed) {
		pthread_mutex_unlock(&channel->mutex);
		return SYS_ERR_CLOSED;
	}

	*out_buffer = channel->queue[channel->head];
	channel->queue[channel->head] = NULL;
	channel->head = (channel->head + 1U) % channel->capacity;
	channel->count--;
	channel->stats.received_count++;
	pthread_cond_signal(&channel->not_full);
	pthread_mutex_unlock(&channel->mutex);
	return SYS_OK;
}

size_t sys_channel_count(sys_channel_t *channel)
{
	size_t count;

	if (channel == NULL) {
		return 0U;
	}
	pthread_mutex_lock(&channel->mutex);
	count = channel->count;
	pthread_mutex_unlock(&channel->mutex);
	return count;
}

sys_err_t sys_channel_get_stats(sys_channel_t *channel, sys_channel_stats_t *out_stats)
{
	if (channel == NULL || out_stats == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	pthread_mutex_lock(&channel->mutex);
	*out_stats = channel->stats;
	out_stats->current_depth = channel->count;
	pthread_mutex_unlock(&channel->mutex);
	return SYS_OK;
}
