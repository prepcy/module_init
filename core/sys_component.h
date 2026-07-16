/**
 * @file sys_component.h
 * @brief 组件依赖、初始化和逆序回滚定义。
 */

#ifndef SYS_COMPONENT_H
#define SYS_COMPONENT_H

#include <stddef.h>
#include <stdint.h>

#include "sys_types.h"

typedef enum {
	SYS_COMPONENT_PHASE_CORE = 0,
	SYS_COMPONENT_PHASE_SERVICE,
	SYS_COMPONENT_PHASE_APPLICATION,
	SYS_COMPONENT_PHASE_COUNT
} sys_component_phase_t;

typedef struct {
	uint32_t id;
	const char *name;
	sys_component_phase_t phase;
	const uint32_t *dependencies;
	size_t dependency_count;
	sys_err_t (*init)(void);
	void (*exit)(void);
} sys_component_desc_t;

/**
 * @brief 将组件描述符导出到链接段，由框架自动发现。
 */
#define SYS_COMPONENT_REGISTER(symbol, component_id, component_name, component_phase, dependency_array,                \
			       dependency_count_value, init_function, exit_function)                                   \
	static const sys_component_desc_t symbol                                                                       \
		__attribute__((used, section("sys_components"), aligned(sizeof(void *)))) = {                          \
			.id = component_id,                                                                            \
			.name = component_name,                                                                        \
			.phase = component_phase,                                                                      \
			.dependencies = dependency_array,                                                              \
			.dependency_count = dependency_count_value,                                                    \
			.init = init_function,                                                                         \
			.exit = exit_function}

#endif
