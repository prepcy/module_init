/**
 * @file sys_thread.c
 * @brief Linux受管线程实现。
 */

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sys_log.h"
#include "sys_thread.h"

#define SYS_THREAD_NAME_CAPACITY 16U

struct sys_thread {
	pthread_t handle;
	atomic_bool stop_requested;
	sys_thread_entry_t entry;
	void *argument;
	char name[SYS_THREAD_NAME_CAPACITY];
};

static atomic_size_t g_active_thread_count;
static atomic_size_t g_unjoined_thread_count;

static void *thread_trampoline(void *argument)
{
	sys_thread_t *thread = argument;
	int ret;
	void *result;

	ret = pthread_setname_np(pthread_self(), thread->name);
	if (ret != 0) {
		SYS_LOG_WARNING_MSG("thread", "failed to set thread name %s: %s", thread->name,
				    sys_error_string(sys_error_from_errno(ret)));
	}
	result = thread->entry(thread, thread->argument);
	atomic_fetch_sub_explicit(&g_active_thread_count, 1U, memory_order_relaxed);
	return result;
}

sys_err_t sys_thread_create(const char *name, sys_thread_entry_t entry, void *argument, sys_thread_t **out_thread)
{
	sys_thread_t *thread;
	int ret;

	if (out_thread == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	*out_thread = NULL;
	if (name == NULL || name[0] == '\0' || strlen(name) >= SYS_THREAD_NAME_CAPACITY || entry == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	thread = calloc(1U, sizeof(*thread));
	if (thread == NULL) {
		return SYS_ERR_NO_MEMORY;
	}
	thread->entry = entry;
	thread->argument = argument;
	(void)snprintf(thread->name, sizeof(thread->name), "%s", name);
	atomic_init(&thread->stop_requested, false);
	atomic_fetch_add_explicit(&g_active_thread_count, 1U, memory_order_relaxed);
	atomic_fetch_add_explicit(&g_unjoined_thread_count, 1U, memory_order_relaxed);
	ret = pthread_create(&thread->handle, NULL, thread_trampoline, thread);
	if (ret != 0) {
		atomic_fetch_sub_explicit(&g_active_thread_count, 1U, memory_order_relaxed);
		atomic_fetch_sub_explicit(&g_unjoined_thread_count, 1U, memory_order_relaxed);
		free(thread);
		return sys_error_from_errno(ret);
	}
	*out_thread = thread;
	return SYS_OK;
}

void sys_thread_request_stop(sys_thread_t *thread)
{
	if (thread != NULL) {
		atomic_store_explicit(&thread->stop_requested, true, memory_order_release);
	}
}

bool sys_thread_stop_requested(const sys_thread_t *thread)
{
	return thread == NULL || atomic_load_explicit(&thread->stop_requested, memory_order_acquire);
}

static void make_join_deadline(struct timespec *deadline, int timeout_ms)
{
	clock_gettime(CLOCK_REALTIME, deadline);
	deadline->tv_sec += timeout_ms / 1000;
	deadline->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
	if (deadline->tv_nsec >= 1000000000L) {
		deadline->tv_sec++;
		deadline->tv_nsec -= 1000000000L;
	}
}

sys_err_t sys_thread_join(sys_thread_t **thread_ptr, int timeout_ms)
{
	sys_thread_t *thread;
	int ret;

	if (thread_ptr == NULL || *thread_ptr == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	thread = *thread_ptr;
	if (pthread_equal(thread->handle, pthread_self())) {
		return SYS_ERR_BUSY;
	}
	if (timeout_ms < 0) {
		ret = pthread_join(thread->handle, NULL);
	} else if (timeout_ms == 0) {
		ret = pthread_tryjoin_np(thread->handle, NULL);
	} else {
		struct timespec deadline;

		make_join_deadline(&deadline, timeout_ms);
		ret = pthread_timedjoin_np(thread->handle, NULL, &deadline);
	}
	if (ret != 0) {
		return ret == ETIMEDOUT || ret == EBUSY ? SYS_ERR_TIMEOUT : sys_error_from_errno(ret);
	}
	free(thread);
	*thread_ptr = NULL;
	atomic_fetch_sub_explicit(&g_unjoined_thread_count, 1U, memory_order_relaxed);
	return SYS_OK;
}

size_t sys_thread_active_count(void)
{
	return atomic_load_explicit(&g_active_thread_count, memory_order_relaxed);
}

size_t sys_thread_unjoined_count(void)
{
	return atomic_load_explicit(&g_unjoined_thread_count, memory_order_relaxed);
}
