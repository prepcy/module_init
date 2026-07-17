/**
 * @file sys_log.h
 * @brief 线程安全的统一框架日志接口。
 */

#ifndef SYS_LOG_H
#define SYS_LOG_H

#include <stdint.h>

typedef enum { SYS_LOG_DEBUG = 0, SYS_LOG_INFO, SYS_LOG_WARNING, SYS_LOG_ERROR } sys_log_level_t;

typedef struct {
	uint64_t timestamp_us;
	long thread_id;
	sys_log_level_t level;
	const char *module;
	const char *file;
	int line;
	const char *message;
} sys_log_record_t;

typedef void (*sys_log_sink_t)(const sys_log_record_t *record, void *private_data);

/** @brief 设置最低输出级别。 */
void sys_log_set_level(sys_log_level_t level);

/** @brief 设置自定义日志接收器；传入 NULL 恢复默认 stderr 输出。 */
void sys_log_set_sink(sys_log_sink_t sink, void *private_data);

/** @brief 写入一条结构化日志。 */
void sys_log_write(sys_log_level_t level, const char *module, const char *file, int line, const char *format, ...)
	__attribute__((format(printf, 5, 6)));

#define SYS_LOG_WRITE(level, module, ...) sys_log_write(level, module, __FILE__, __LINE__, __VA_ARGS__)
#define SYS_LOG_DEBUG_MSG(module, ...) SYS_LOG_WRITE(SYS_LOG_DEBUG, module, __VA_ARGS__)
#define SYS_LOG_INFO_MSG(module, ...) SYS_LOG_WRITE(SYS_LOG_INFO, module, __VA_ARGS__)
#define SYS_LOG_WARNING_MSG(module, ...) SYS_LOG_WRITE(SYS_LOG_WARNING, module, __VA_ARGS__)
#define SYS_LOG_ERROR_MSG(module, ...) SYS_LOG_WRITE(SYS_LOG_ERROR, module, __VA_ARGS__)

#endif
