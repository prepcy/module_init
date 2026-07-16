/**
 * @file sys_event.c
 * @brief 有界异步事件总线实现。
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys_event.h"
#include "sys_internal.h"

#define SYS_EVENT_MAX_SUBSCRIBERS 64U
#define SYS_EVENT_QUEUE_CAPACITY 64U
#define SYS_EVENT_MAX_PAYLOAD (64U * 1024U)

struct sys_event_subscription {
	uint32_t event_id;
	sys_event_callback_t callback;
	void *private_data;
	bool active;
	size_t in_flight;
};

typedef struct {
	uint32_t event_id;
	void *data;
	size_t size;
	sys_event_subscription_t *subscribers[SYS_EVENT_MAX_SUBSCRIBERS];
	size_t subscriber_count;
} sys_event_work_t;

static sys_event_subscription_t *g_subscribers[SYS_EVENT_MAX_SUBSCRIBERS];
static size_t g_subscriber_count;
static sys_event_work_t g_event_queue[SYS_EVENT_QUEUE_CAPACITY];
static size_t g_queue_head;
static size_t g_queue_count;
static bool g_event_running;
static bool g_event_accepting;
static pthread_t g_event_thread;
static pthread_mutex_t g_event_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_event_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_subscription_cond = PTHREAD_COND_INITIALIZER;
static _Thread_local sys_event_subscription_t *g_current_subscription;
static _Thread_local unsigned int g_event_callback_depth;

static void finish_callback(sys_event_subscription_t *subscription)
{
	pthread_mutex_lock(&g_event_mutex);
	if (subscription->in_flight > 0U) {
		subscription->in_flight--;
	}
	if (!subscription->active && subscription->in_flight == 0U) {
		pthread_cond_broadcast(&g_subscription_cond);
	}
	pthread_mutex_unlock(&g_event_mutex);
}

static void *event_worker(void *argument)
{
	(void)argument;
	for (;;) {
		sys_event_work_t work;

		pthread_mutex_lock(&g_event_mutex);
		while (g_queue_count == 0U && g_event_running) {
			pthread_cond_wait(&g_event_cond, &g_event_mutex);
		}
		if (g_queue_count == 0U && !g_event_running) {
			pthread_mutex_unlock(&g_event_mutex);
			break;
		}

		work = g_event_queue[g_queue_head];
		g_event_queue[g_queue_head] = (sys_event_work_t){0};
		g_queue_head = (g_queue_head + 1U) % SYS_EVENT_QUEUE_CAPACITY;
		g_queue_count--;
		pthread_mutex_unlock(&g_event_mutex);

		sys_event_t event = {.id = work.event_id, .data = work.data, .size = work.size};
		for (size_t i = 0; i < work.subscriber_count; i++) {
			sys_err_t ret;
			sys_event_subscription_t *subscription = work.subscribers[i];

			g_current_subscription = subscription;
			g_event_callback_depth++;
			ret = subscription->callback(&event, subscription->private_data);
			g_event_callback_depth--;
			g_current_subscription = NULL;
			if (ret != SYS_OK) {
				fprintf(stderr, "[event] async callback for %u failed: %d\n", event.id, ret);
			}
			finish_callback(subscription);
		}
		free(work.data);
	}
	return NULL;
}

sys_err_t sys_event_bus_start(void)
{
	int ret;

	pthread_mutex_lock(&g_event_mutex);
	if (g_event_running) {
		pthread_mutex_unlock(&g_event_mutex);
		return SYS_ERR_STATE;
	}
	g_queue_head = 0U;
	g_queue_count = 0U;
	g_event_running = true;
	g_event_accepting = true;
	pthread_mutex_unlock(&g_event_mutex);

	ret = pthread_create(&g_event_thread, NULL, event_worker, NULL);
	if (ret != 0) {
		pthread_mutex_lock(&g_event_mutex);
		g_event_running = false;
		g_event_accepting = false;
		pthread_mutex_unlock(&g_event_mutex);
		return SYS_ERR_GENERIC;
	}
	return SYS_OK;
}

void sys_event_bus_stop(void)
{
	pthread_mutex_lock(&g_event_mutex);
	if (!g_event_running) {
		g_event_accepting = false;
		pthread_mutex_unlock(&g_event_mutex);
		return;
	}
	g_event_accepting = false;
	g_event_running = false;
	pthread_cond_broadcast(&g_event_cond);
	pthread_mutex_unlock(&g_event_mutex);
	pthread_join(g_event_thread, NULL);

	pthread_mutex_lock(&g_event_mutex);
	for (size_t i = 0; i < g_subscriber_count; i++) {
		free(g_subscribers[i]);
		g_subscribers[i] = NULL;
	}
	g_subscriber_count = 0U;
	pthread_mutex_unlock(&g_event_mutex);
}

sys_err_t sys_event_subscribe(uint32_t event_id, sys_event_callback_t callback, void *private_data,
			      sys_event_subscription_t **out_subscription)
{
	sys_event_subscription_t *subscription;

	if (event_id == 0U || callback == NULL || out_subscription == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	*out_subscription = NULL;
	subscription = calloc(1U, sizeof(*subscription));
	if (subscription == NULL) {
		return SYS_ERR_NO_MEMORY;
	}
	subscription->event_id = event_id;
	subscription->callback = callback;
	subscription->private_data = private_data;
	subscription->active = true;

	pthread_mutex_lock(&g_event_mutex);
	if (!g_event_accepting) {
		pthread_mutex_unlock(&g_event_mutex);
		free(subscription);
		return SYS_ERR_CLOSED;
	}
	if (g_subscriber_count >= SYS_EVENT_MAX_SUBSCRIBERS) {
		pthread_mutex_unlock(&g_event_mutex);
		free(subscription);
		return SYS_ERR_NO_MEMORY;
	}
	for (size_t i = 0; i < g_subscriber_count; i++) {
		if (g_subscribers[i]->event_id == event_id && g_subscribers[i]->callback == callback &&
		    g_subscribers[i]->private_data == private_data) {
			pthread_mutex_unlock(&g_event_mutex);
			free(subscription);
			return SYS_ERR_ALREADY_EXISTS;
		}
	}
	g_subscribers[g_subscriber_count++] = subscription;
	pthread_mutex_unlock(&g_event_mutex);
	*out_subscription = subscription;
	return SYS_OK;
}

sys_err_t sys_event_unsubscribe(sys_event_subscription_t **subscription_ptr)
{
	sys_event_subscription_t *subscription;
	bool found = false;

	if (subscription_ptr == NULL || *subscription_ptr == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	subscription = *subscription_ptr;
	if (g_event_callback_depth > 0U || g_current_subscription == subscription) {
		return SYS_ERR_BUSY;
	}

	pthread_mutex_lock(&g_event_mutex);
	for (size_t i = 0; i < g_subscriber_count; i++) {
		if (g_subscribers[i] == subscription) {
			for (size_t j = i + 1U; j < g_subscriber_count; j++) {
				g_subscribers[j - 1U] = g_subscribers[j];
			}
			g_subscriber_count--;
			g_subscribers[g_subscriber_count] = NULL;
			found = true;
			break;
		}
	}
	if (!found) {
		pthread_mutex_unlock(&g_event_mutex);
		return SYS_ERR_NOT_FOUND;
	}

	subscription->active = false;
	while (subscription->in_flight > 0U) {
		pthread_cond_wait(&g_subscription_cond, &g_event_mutex);
	}
	pthread_mutex_unlock(&g_event_mutex);
	free(subscription);
	*subscription_ptr = NULL;
	return SYS_OK;
}

static size_t retain_matching_subscribers(uint32_t event_id, sys_event_subscription_t **out_subscribers)
{
	size_t count = 0U;

	for (size_t i = 0; i < g_subscriber_count; i++) {
		sys_event_subscription_t *subscription = g_subscribers[i];

		if (subscription->active && subscription->event_id == event_id) {
			subscription->in_flight++;
			out_subscribers[count++] = subscription;
		}
	}
	return count;
}

sys_err_t sys_event_publish_sync(uint32_t event_id, const void *data, size_t size)
{
	sys_event_subscription_t *subscribers[SYS_EVENT_MAX_SUBSCRIBERS];
	sys_event_t event = {.id = event_id, .data = data, .size = size};
	sys_err_t first_error = SYS_OK;
	size_t subscriber_count;

	if (event_id == 0U || (size > 0U && data == NULL)) {
		return SYS_ERR_INVALID_PARAM;
	}
	pthread_mutex_lock(&g_event_mutex);
	if (!g_event_accepting) {
		pthread_mutex_unlock(&g_event_mutex);
		return SYS_ERR_CLOSED;
	}
	subscriber_count = retain_matching_subscribers(event_id, subscribers);
	pthread_mutex_unlock(&g_event_mutex);

	for (size_t i = 0; i < subscriber_count; i++) {
		sys_err_t ret;

		g_current_subscription = subscribers[i];
		g_event_callback_depth++;
		ret = subscribers[i]->callback(&event, subscribers[i]->private_data);
		g_event_callback_depth--;
		g_current_subscription = NULL;
		if (first_error == SYS_OK && ret != SYS_OK) {
			first_error = ret;
		}
		finish_callback(subscribers[i]);
	}
	return first_error;
}

sys_err_t sys_event_publish_async(uint32_t event_id, const void *data, size_t size)
{
	sys_event_work_t *work;
	void *payload = NULL;
	size_t queue_index;

	if (event_id == 0U || size > SYS_EVENT_MAX_PAYLOAD || (size > 0U && data == NULL)) {
		return SYS_ERR_INVALID_PARAM;
	}
	if (size > 0U) {
		payload = malloc(size);
		if (payload == NULL) {
			return SYS_ERR_NO_MEMORY;
		}
		memcpy(payload, data, size);
	}

	pthread_mutex_lock(&g_event_mutex);
	if (!g_event_accepting) {
		pthread_mutex_unlock(&g_event_mutex);
		free(payload);
		return SYS_ERR_CLOSED;
	}
	if (g_queue_count >= SYS_EVENT_QUEUE_CAPACITY) {
		pthread_mutex_unlock(&g_event_mutex);
		free(payload);
		return SYS_ERR_QUEUE_FULL;
	}

	queue_index = (g_queue_head + g_queue_count) % SYS_EVENT_QUEUE_CAPACITY;
	work = &g_event_queue[queue_index];
	*work = (sys_event_work_t){0};
	work->event_id = event_id;
	work->data = payload;
	work->size = size;
	work->subscriber_count = retain_matching_subscribers(event_id, work->subscribers);
	if (work->subscriber_count == 0U) {
		*work = (sys_event_work_t){0};
		pthread_mutex_unlock(&g_event_mutex);
		free(payload);
		return SYS_OK;
	}

	g_queue_count++;
	pthread_cond_signal(&g_event_cond);
	pthread_mutex_unlock(&g_event_mutex);
	return SYS_OK;
}
