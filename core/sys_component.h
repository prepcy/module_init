/**
 * @file sys_component.h
 * @brief 应用组件依赖、生命周期和运行状态定义。
 */

#ifndef SYS_COMPONENT_H
#define SYS_COMPONENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sys_types.h"

typedef enum {
	SYS_COMPONENT_PHASE_CORE = 0,
	SYS_COMPONENT_PHASE_SERVICE,
	SYS_COMPONENT_PHASE_APPLICATION,
	SYS_COMPONENT_PHASE_COUNT
} sys_component_phase_t;

typedef enum { SYS_COMPONENT_REQUIRED = 0, SYS_COMPONENT_OPTIONAL } sys_component_policy_t;

typedef enum {
	SYS_COMPONENT_DISCOVERED = 0,
	SYS_COMPONENT_INITIALIZING,
	SYS_COMPONENT_INITIALIZED,
	SYS_COMPONENT_STARTING,
	SYS_COMPONENT_RUNNING,
	SYS_COMPONENT_STOPPING,
	SYS_COMPONENT_STOPPED,
	SYS_COMPONENT_FAILED,
	SYS_COMPONENT_SKIPPED
} sys_component_state_t;

typedef struct {
	uint32_t id;
	const char *name;
	sys_component_phase_t phase;
	sys_component_policy_t policy;
	const uint32_t *dependencies;
	size_t dependency_count;
	sys_err_t (*init)(void);
	sys_err_t (*start)(void);
	sys_err_t (*stop)(void);
	void (*deinit)(void);
	sys_err_t (*health_check)(void);
} sys_component_desc_t;

typedef struct {
	uint32_t id;
	const char *name;
	sys_component_state_t state;
	sys_err_t last_error;
	bool required;
} sys_component_status_t;

/**
 * @brief 将组件描述符导出到链接段，由框架自动发现。
 */
#define SYS_COMPONENT_REGISTER(symbol, ...)                                                                            \
	static const sys_component_desc_t symbol                                                                       \
		__attribute__((used, section("sys_components"), aligned(sizeof(void *)))) = {__VA_ARGS__}

/** @brief 获取组件状态快照数量。 */
size_t sys_component_get_status(sys_component_status_t *statuses, size_t capacity);

/** @brief 执行指定组件的健康检查。 */
sys_err_t sys_component_check_health(uint32_t component_id);

/** @brief 将组件状态转换为可读文本。 */
const char *sys_component_state_string(sys_component_state_t state);

#endif
