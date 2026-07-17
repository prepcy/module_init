/**
 * @file sys_component.c
 * @brief 四阶段组件生命周期和依赖解析实现。
 */

#include <pthread.h>
#include <stdbool.h>

#include "sys_component.h"
#include "sys_internal.h"
#include "sys_log.h"

#define SYS_COMPONENT_MAX_COUNT 64U

typedef struct {
	const sys_component_desc_t *desc;
	sys_component_state_t state;
	sys_err_t last_error;
	bool initialized;
	bool started;
} sys_component_runtime_t;

typedef enum { DEPENDENCIES_WAITING = 0, DEPENDENCIES_READY, DEPENDENCIES_FAILED } dependency_result_t;

extern const sys_component_desc_t __start_sys_components[];
extern const sys_component_desc_t __stop_sys_components[];

static const sys_component_desc_t g_component_sentinel
	__attribute__((used, section("sys_components"), aligned(sizeof(void *)))) = {0};

static sys_component_runtime_t g_components[SYS_COMPONENT_MAX_COUNT];
static size_t g_component_count;
static sys_component_runtime_t *g_initialized_order[SYS_COMPONENT_MAX_COUNT];
static size_t g_initialized_count;
static sys_component_runtime_t *g_started_order[SYS_COMPONENT_MAX_COUNT];
static size_t g_started_count;
static pthread_mutex_t g_component_mutex = PTHREAD_MUTEX_INITIALIZER;

static void update_state(sys_component_runtime_t *component, sys_component_state_t state, sys_err_t error)
{
	pthread_mutex_lock(&g_component_mutex);
	component->state = state;
	component->last_error = error;
	pthread_mutex_unlock(&g_component_mutex);
}

static sys_component_runtime_t *find_component(uint32_t id)
{
	for (size_t i = 0; i < g_component_count; i++) {
		if (g_components[i].desc->id == id) {
			return &g_components[i];
		}
	}
	return NULL;
}

static dependency_result_t check_dependencies(const sys_component_runtime_t *component, bool require_running)
{
	for (size_t i = 0; i < component->desc->dependency_count; i++) {
		sys_component_runtime_t *dependency = find_component(component->desc->dependencies[i]);

		if (dependency == NULL || dependency->state == SYS_COMPONENT_FAILED ||
		    dependency->state == SYS_COMPONENT_SKIPPED) {
			return DEPENDENCIES_FAILED;
		}
		if (require_running && dependency->state != SYS_COMPONENT_RUNNING) {
			return DEPENDENCIES_WAITING;
		}
		if (!require_running && !dependency->initialized) {
			return DEPENDENCIES_WAITING;
		}
	}
	return DEPENDENCIES_READY;
}

static sys_err_t validate_components(void)
{
	for (size_t i = 0; i < g_component_count; i++) {
		const sys_component_desc_t *desc = g_components[i].desc;

		if (desc->id == 0U || desc->name == NULL || desc->init == NULL ||
		    desc->phase >= SYS_COMPONENT_PHASE_COUNT || desc->policy > SYS_COMPONENT_OPTIONAL) {
			return SYS_ERR_INVALID_PARAM;
		}
		for (size_t j = i + 1U; j < g_component_count; j++) {
			if (g_components[j].desc->id == desc->id) {
				SYS_LOG_ERROR_MSG("component", "duplicate component id: %u", desc->id);
				return SYS_ERR_ALREADY_EXISTS;
			}
		}
		for (size_t j = 0; j < desc->dependency_count; j++) {
			sys_component_runtime_t *dependency = find_component(desc->dependencies[j]);

			if (dependency == NULL || dependency->desc->phase > desc->phase) {
				SYS_LOG_ERROR_MSG("component", "invalid dependency for component %s", desc->name);
				return SYS_ERR_DEPENDENCY;
			}
		}
	}
	return SYS_OK;
}

static sys_err_t collect_components(void)
{
	const uintptr_t section_start = (uintptr_t)(const void *)__start_sys_components;
	const uintptr_t section_stop = (uintptr_t)(const void *)__stop_sys_components;
	size_t section_count;

	if (section_stop < section_start || (section_stop - section_start) % sizeof(sys_component_desc_t) != 0U) {
		return SYS_ERR_STATE;
	}
	section_count = (section_stop - section_start) / sizeof(sys_component_desc_t);
	if (section_count > SYS_COMPONENT_MAX_COUNT) {
		return SYS_ERR_OVERFLOW;
	}
	g_component_count = 0U;
	g_initialized_count = 0U;
	g_started_count = 0U;
	for (size_t i = 0; i < section_count; i++) {
		const sys_component_desc_t *desc = &__start_sys_components[i];

		if (desc->init == NULL) {
			continue;
		}
		g_components[g_component_count] = (sys_component_runtime_t){
			.desc = desc, .state = SYS_COMPONENT_DISCOVERED, .last_error = SYS_OK};
		g_component_count++;
	}
	return validate_components();
}

static sys_err_t handle_dependency_failure(sys_component_runtime_t *component)
{
	update_state(component, SYS_COMPONENT_SKIPPED, SYS_ERR_DEPENDENCY);
	if (component->desc->policy == SYS_COMPONENT_REQUIRED) {
		SYS_LOG_ERROR_MSG("component", "required component %s has a failed dependency", component->desc->name);
		return SYS_ERR_DEPENDENCY;
	}
	return SYS_OK;
}

static sys_err_t initialize_component(sys_component_runtime_t *component)
{
	update_state(component, SYS_COMPONENT_INITIALIZING, SYS_OK);
	sys_err_t ret = component->desc->init();
	if (ret != SYS_OK) {
		update_state(component, SYS_COMPONENT_FAILED, ret);
		SYS_LOG_ERROR_MSG("component", "component %s init failed: %s", component->desc->name,
				  sys_error_string(ret));
		return component->desc->policy == SYS_COMPONENT_REQUIRED ? ret : SYS_OK;
	}
	component->initialized = true;
	g_initialized_order[g_initialized_count++] = component;
	update_state(component, SYS_COMPONENT_INITIALIZED, SYS_OK);
	return SYS_OK;
}

static sys_err_t initialize_phase(sys_component_phase_t phase)
{
	for (;;) {
		size_t pending_count = 0U;
		bool skipped_component = false;
		sys_component_runtime_t *candidate = NULL;

		for (size_t i = 0; i < g_component_count; i++) {
			sys_component_runtime_t *component = &g_components[i];
			dependency_result_t dependencies;

			if (component->state != SYS_COMPONENT_DISCOVERED || component->desc->phase != phase) {
				continue;
			}
			pending_count++;
			dependencies = check_dependencies(component, false);
			if (dependencies == DEPENDENCIES_FAILED) {
				sys_err_t ret = handle_dependency_failure(component);
				if (ret != SYS_OK) {
					return ret;
				}
				skipped_component = true;
				continue;
			}
			if (dependencies == DEPENDENCIES_READY &&
			    (candidate == NULL || component->desc->id < candidate->desc->id)) {
				candidate = component;
			}
		}
		if (pending_count == 0U) {
			return SYS_OK;
		}
		if (candidate == NULL) {
			if (skipped_component) {
				continue;
			}
			return SYS_ERR_DEPENDENCY;
		}
		sys_err_t ret = initialize_component(candidate);
		if (ret != SYS_OK) {
			return ret;
		}
	}
}

static sys_err_t start_component(sys_component_runtime_t *component)
{
	if (check_dependencies(component, true) != DEPENDENCIES_READY) {
		sys_err_t ret = handle_dependency_failure(component);
		if (ret == SYS_OK && component->initialized) {
			if (component->desc->deinit != NULL) {
				component->desc->deinit();
			}
			component->initialized = false;
		}
		return ret;
	}
	update_state(component, SYS_COMPONENT_STARTING, SYS_OK);
	if (component->desc->start != NULL) {
		sys_err_t ret = component->desc->start();

		if (ret != SYS_OK) {
			update_state(component, SYS_COMPONENT_FAILED, ret);
			SYS_LOG_ERROR_MSG("component", "component %s start failed: %s", component->desc->name,
					  sys_error_string(ret));
			if (component->desc->policy == SYS_COMPONENT_REQUIRED) {
				return ret;
			}
			if (component->desc->deinit != NULL) {
				component->desc->deinit();
			}
			component->initialized = false;
			return SYS_OK;
		}
	}
	component->started = true;
	g_started_order[g_started_count++] = component;
	update_state(component, SYS_COMPONENT_RUNNING, SYS_OK);
	return SYS_OK;
}

static sys_err_t stop_started_components(void)
{
	sys_err_t first_error = SYS_OK;

	while (g_started_count > 0U) {
		sys_component_runtime_t *component = g_started_order[--g_started_count];
		sys_err_t ret = SYS_OK;

		update_state(component, SYS_COMPONENT_STOPPING, component->last_error);
		if (component->desc->stop != NULL) {
			ret = component->desc->stop();
		}
		component->started = false;
		if (ret != SYS_OK) {
			if (first_error == SYS_OK) {
				first_error = ret;
			}
			update_state(component, SYS_COMPONENT_FAILED, ret);
			SYS_LOG_ERROR_MSG("component", "component %s stop failed: %s", component->desc->name,
					  sys_error_string(ret));
		} else {
			update_state(component, SYS_COMPONENT_STOPPED, component->last_error);
		}
	}
	return first_error;
}

static void deinitialize_components(void)
{
	while (g_initialized_count > 0U) {
		sys_component_runtime_t *component = g_initialized_order[--g_initialized_count];

		if (component->initialized) {
			if (component->desc->deinit != NULL) {
				component->desc->deinit();
			}
			component->initialized = false;
			if (component->state != SYS_COMPONENT_FAILED && component->state != SYS_COMPONENT_SKIPPED) {
				update_state(component, SYS_COMPONENT_STOPPED, component->last_error);
			}
		}
	}
}

static sys_err_t rollback_components(void)
{
	sys_err_t ret = stop_started_components();

	deinitialize_components();
	return ret;
}

sys_err_t sys_component_start_all(void)
{
	sys_err_t ret = collect_components();
	if (ret != SYS_OK) {
		return ret;
	}
	for (sys_component_phase_t phase = SYS_COMPONENT_PHASE_CORE; phase < SYS_COMPONENT_PHASE_COUNT; phase++) {
		ret = initialize_phase(phase);
		if (ret != SYS_OK) {
			(void)rollback_components();
			return ret;
		}
	}
	for (size_t i = 0; i < g_initialized_count; i++) {
		ret = start_component(g_initialized_order[i]);
		if (ret != SYS_OK) {
			(void)rollback_components();
			return ret;
		}
	}
	return SYS_OK;
}

sys_err_t sys_component_stop_all(void)
{
	return rollback_components();
}

size_t sys_component_get_status(sys_component_status_t *statuses, size_t capacity)
{
	pthread_mutex_lock(&g_component_mutex);
	size_t total_count = g_component_count;
	size_t copy_count = statuses == NULL ? 0U : (capacity < total_count ? capacity : total_count);
	for (size_t i = 0; i < copy_count; i++) {
		statuses[i] =
			(sys_component_status_t){.id = g_components[i].desc->id,
						 .name = g_components[i].desc->name,
						 .state = g_components[i].state,
						 .last_error = g_components[i].last_error,
						 .required = g_components[i].desc->policy == SYS_COMPONENT_REQUIRED};
	}
	pthread_mutex_unlock(&g_component_mutex);
	return total_count;
}

sys_err_t sys_component_check_health(uint32_t component_id)
{
	sys_err_t (*health_check)(void) = NULL;
	sys_component_state_t state = SYS_COMPONENT_DISCOVERED;
	bool found = false;

	pthread_mutex_lock(&g_component_mutex);
	for (size_t i = 0; i < g_component_count; i++) {
		if (g_components[i].desc->id == component_id) {
			state = g_components[i].state;
			health_check = g_components[i].desc->health_check;
			found = true;
			break;
		}
	}
	pthread_mutex_unlock(&g_component_mutex);
	if (!found) {
		return SYS_ERR_NOT_FOUND;
	}
	if (state != SYS_COMPONENT_RUNNING) {
		return SYS_ERR_STATE;
	}
	return health_check == NULL ? SYS_OK : health_check();
}

const char *sys_component_state_string(sys_component_state_t state)
{
	static const char *const names[] = {"discovered", "initializing", "initialized", "starting", "running",
					    "stopping",	  "stopped",	  "failed",	 "skipped"};
	return state < SYS_ARRAY_SIZE(names) ? names[state] : "unknown";
}
