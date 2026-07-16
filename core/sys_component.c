/**
 * @file sys_component.c
 * @brief 组件依赖解析和生命周期实现。
 */

#include <stdbool.h>
#include <stdio.h>

#include "sys_component.h"
#include "sys_internal.h"

#define SYS_COMPONENT_MAX_COUNT 64U

typedef struct {
	const sys_component_desc_t *desc;
	bool started;
} sys_component_runtime_t;

extern const sys_component_desc_t __start_sys_components[];
extern const sys_component_desc_t __stop_sys_components[];

static const sys_component_desc_t g_component_sentinel
	__attribute__((used, section("sys_components"), aligned(sizeof(void *)))) = {0};

static const sys_component_desc_t *g_started_components[SYS_COMPONENT_MAX_COUNT];
static size_t g_started_count;

static const sys_component_desc_t *find_component(const sys_component_runtime_t *components, size_t count, uint32_t id)
{
	for (size_t i = 0; i < count; i++) {
		if (components[i].desc->id == id) {
			return components[i].desc;
		}
	}
	return NULL;
}

static bool dependency_started(const sys_component_runtime_t *components, size_t count, uint32_t id)
{
	for (size_t i = 0; i < count; i++) {
		if (components[i].desc->id == id) {
			return components[i].started;
		}
	}
	return false;
}

static bool component_ready(const sys_component_runtime_t *component, const sys_component_runtime_t *components,
			    size_t count)
{
	for (size_t i = 0; i < component->desc->dependency_count; i++) {
		if (!dependency_started(components, count, component->desc->dependencies[i])) {
			return false;
		}
	}
	return true;
}

static void rollback_started_components(void)
{
	while (g_started_count > 0U) {
		const sys_component_desc_t *desc = g_started_components[--g_started_count];

		if (desc->exit != NULL) {
			desc->exit();
		}
	}
}

static sys_err_t validate_components(const sys_component_runtime_t *components, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		const sys_component_desc_t *desc = components[i].desc;

		if (desc->id == 0U || desc->name == NULL || desc->init == NULL ||
		    desc->phase >= SYS_COMPONENT_PHASE_COUNT) {
			return SYS_ERR_INVALID_PARAM;
		}
		for (size_t j = i + 1U; j < count; j++) {
			if (components[j].desc->id == desc->id) {
				fprintf(stderr, "[core] duplicate component id: %u\n", desc->id);
				return SYS_ERR_ALREADY_EXISTS;
			}
		}
		for (size_t j = 0; j < desc->dependency_count; j++) {
			const sys_component_desc_t *dependency;

			dependency = find_component(components, count, desc->dependencies[j]);
			if (dependency == NULL) {
				fprintf(stderr, "[core] component %s misses dependency %u\n", desc->name,
					desc->dependencies[j]);
				return SYS_ERR_DEPENDENCY;
			}
			if (dependency->phase > desc->phase) {
				fprintf(stderr, "[core] component %s depends on a later phase\n", desc->name);
				return SYS_ERR_DEPENDENCY;
			}
		}
	}
	return SYS_OK;
}

static sys_err_t collect_components(sys_component_runtime_t *components, size_t *out_count)
{
	/* GNU ld guarantees that both symbols delimit the same output section. */
	// cppcheck-suppress comparePointers
	size_t section_count = (size_t)(__stop_sys_components - __start_sys_components);

	if (g_started_count != 0U || section_count > SYS_COMPONENT_MAX_COUNT) {
		return SYS_ERR_STATE;
	}

	for (size_t i = 0; i < section_count; i++) {
		const sys_component_desc_t *desc = &__start_sys_components[i];

		if (desc->init != NULL) {
			components[*out_count].desc = desc;
			components[*out_count].started = false;
			(*out_count)++;
		}
	}
	return validate_components(components, *out_count);
}

static sys_component_runtime_t *find_ready_component(sys_component_runtime_t *components, size_t count,
						     sys_component_phase_t phase, size_t *out_pending)
{
	sys_component_runtime_t *candidate = NULL;

	*out_pending = 0U;
	for (size_t i = 0; i < count; i++) {
		sys_component_runtime_t *component = &components[i];

		if (component->started || component->desc->phase != phase) {
			continue;
		}
		(*out_pending)++;
		if (component_ready(component, components, count) &&
		    (candidate == NULL || component->desc->id < candidate->desc->id)) {
			candidate = component;
		}
	}
	return candidate;
}

static sys_err_t start_component_phase(sys_component_runtime_t *components, size_t count, sys_component_phase_t phase)
{
	for (;;) {
		size_t pending_count;
		sys_component_runtime_t *candidate = find_ready_component(components, count, phase, &pending_count);

		if (pending_count == 0U) {
			return SYS_OK;
		}
		if (candidate == NULL) {
			fprintf(stderr, "[core] component dependency cycle in phase %d\n", phase);
			return SYS_ERR_DEPENDENCY;
		}

		sys_err_t ret = candidate->desc->init();
		if (ret != SYS_OK) {
			fprintf(stderr, "[core] component %s failed: %d\n", candidate->desc->name, ret);
			return ret;
		}
		candidate->started = true;
		g_started_components[g_started_count++] = candidate->desc;
	}
}

sys_err_t sys_component_start_all(void)
{
	sys_component_runtime_t components[SYS_COMPONENT_MAX_COUNT];
	size_t component_count = 0U;
	sys_err_t ret = collect_components(components, &component_count);

	if (ret != SYS_OK) {
		return ret;
	}

	for (sys_component_phase_t phase = SYS_COMPONENT_PHASE_CORE; phase < SYS_COMPONENT_PHASE_COUNT; phase++) {
		ret = start_component_phase(components, component_count, phase);
		if (ret != SYS_OK) {
			rollback_started_components();
			return ret;
		}
	}

	return SYS_OK;
}

void sys_component_stop_all(void)
{
	rollback_started_components();
}
