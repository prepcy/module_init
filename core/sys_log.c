/**
 * @file sys_log.c
 * @brief 统一日志实现。
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "sys_log.h"

#define SYS_LOG_MESSAGE_CAPACITY 512U

static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static sys_log_level_t g_log_level = SYS_LOG_INFO;
static sys_log_sink_t g_log_sink;
static void *g_log_sink_data;

static const char *level_name(sys_log_level_t level)
{
	static const char *const names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
	return level <= SYS_LOG_ERROR ? names[level] : "UNKNOWN";
}

static uint64_t realtime_microseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

static void default_sink(const sys_log_record_t *record)
{
	time_t seconds = (time_t)(record->timestamp_us / 1000000U);
	struct tm local_time;
	char time_buffer[32];

	localtime_r(&seconds, &local_time);
	strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &local_time);
	fprintf(stderr, "%s.%06llu %-5s [%s] [tid=%ld] %s (%s:%d)\n", time_buffer,
		(unsigned long long)(record->timestamp_us % 1000000U), level_name(record->level), record->module,
		record->thread_id, record->message, record->file, record->line);
}

void sys_log_set_level(sys_log_level_t level)
{
	if (level > SYS_LOG_ERROR) {
		return;
	}
	pthread_mutex_lock(&g_log_mutex);
	g_log_level = level;
	pthread_mutex_unlock(&g_log_mutex);
}

void sys_log_set_sink(sys_log_sink_t sink, void *private_data)
{
	pthread_mutex_lock(&g_log_mutex);
	g_log_sink = sink;
	g_log_sink_data = private_data;
	pthread_mutex_unlock(&g_log_mutex);
}

void sys_log_write(sys_log_level_t level, const char *module, const char *file, int line, const char *format, ...)
{
	char message[SYS_LOG_MESSAGE_CAPACITY];
	sys_log_sink_t sink;
	void *sink_data;
	va_list arguments;

	if (module == NULL || file == NULL || format == NULL) {
		return;
	}
	pthread_mutex_lock(&g_log_mutex);
	if (level < g_log_level) {
		pthread_mutex_unlock(&g_log_mutex);
		return;
	}
	sink = g_log_sink;
	sink_data = g_log_sink_data;
	pthread_mutex_unlock(&g_log_mutex);

	va_start(arguments, format);
	/* Clang 10's analyzer does not model va_start for the platform array-form va_list. */
	// NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized)
	(void)vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	sys_log_record_t record = {.timestamp_us = realtime_microseconds(),
				   .thread_id = (long)syscall(SYS_gettid),
				   .level = level,
				   .module = module,
				   .file = file,
				   .line = line,
				   .message = message};
	if (sink != NULL) {
		sink(&record, sink_data);
		return;
	}
	pthread_mutex_lock(&g_log_mutex);
	default_sink(&record);
	pthread_mutex_unlock(&g_log_mutex);
}
