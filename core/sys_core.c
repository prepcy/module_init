/**
 * @file sys_core.c
 * @brief 框架总生命周期实现。
 */

#include <pthread.h>

#include "sys_core.h"
#include "sys_internal.h"
#include "sys_log.h"
#include "sys_thread.h"

typedef enum { SYS_CORE_STOPPED = 0, SYS_CORE_STARTING, SYS_CORE_RUNNING, SYS_CORE_STOPPING } sys_core_state_t;

static pthread_mutex_t g_core_mutex = PTHREAD_MUTEX_INITIALIZER;
static sys_core_state_t g_core_state = SYS_CORE_STOPPED;

sys_err_t sys_core_init(void)
{
	sys_err_t ret;

	pthread_mutex_lock(&g_core_mutex);
	if (g_core_state != SYS_CORE_STOPPED) {
		pthread_mutex_unlock(&g_core_mutex);
		return SYS_ERR_STATE;
	}
	g_core_state = SYS_CORE_STARTING;
	pthread_mutex_unlock(&g_core_mutex);

	sys_service_registry_reset();
	ret = sys_event_bus_start();
	if (ret == SYS_OK) {
		ret = sys_component_start_all();
	}
	if (ret != SYS_OK) {
		sys_err_t stop_ret = sys_event_bus_stop();

		if (stop_ret != SYS_OK) {
			SYS_LOG_ERROR_MSG("core", "event bus rollback failed: %s", sys_error_string(stop_ret));
		}
		sys_service_registry_reset();
	}

	pthread_mutex_lock(&g_core_mutex);
	g_core_state = ret == SYS_OK ? SYS_CORE_RUNNING : SYS_CORE_STOPPED;
	pthread_mutex_unlock(&g_core_mutex);
	return ret;
}

sys_err_t sys_core_shutdown(void)
{
	sys_err_t component_ret;
	sys_err_t event_ret;
	sys_err_t ret = SYS_OK;

	pthread_mutex_lock(&g_core_mutex);
	if (g_core_state != SYS_CORE_RUNNING) {
		pthread_mutex_unlock(&g_core_mutex);
		return SYS_ERR_STATE;
	}
	g_core_state = SYS_CORE_STOPPING;
	pthread_mutex_unlock(&g_core_mutex);

	component_ret = sys_component_stop_all();
	event_ret = sys_event_bus_stop();
	if (component_ret != SYS_OK) {
		ret = component_ret;
	} else if (event_ret != SYS_OK) {
		ret = event_ret;
	}
	if (sys_thread_unjoined_count() != 0U) {
		SYS_LOG_ERROR_MSG("core", "%zu managed thread handles remain after component shutdown",
				  sys_thread_unjoined_count());
		if (ret == SYS_OK) {
			ret = SYS_ERR_BUSY;
		}
	}
	sys_service_registry_reset();

	pthread_mutex_lock(&g_core_mutex);
	g_core_state = SYS_CORE_STOPPED;
	pthread_mutex_unlock(&g_core_mutex);
	return ret;
}

bool sys_core_is_running(void)
{
	bool is_running;

	pthread_mutex_lock(&g_core_mutex);
	is_running = g_core_state == SYS_CORE_RUNNING;
	pthread_mutex_unlock(&g_core_mutex);
	return is_running;
}

void sys_core_get_status(sys_core_status_t *out_status)
{
	sys_component_status_t components[64];
	size_t component_count;

	if (out_status == NULL) {
		return;
	}
	*out_status = (sys_core_status_t){0};
	out_status->running = sys_core_is_running();
	component_count = sys_component_get_status(components, SYS_ARRAY_SIZE(components));
	out_status->component_count = component_count;
	for (size_t i = 0; i < component_count && i < SYS_ARRAY_SIZE(components); i++) {
		if (components[i].state == SYS_COMPONENT_RUNNING) {
			out_status->running_component_count++;
		}
		if (components[i].state == SYS_COMPONENT_FAILED || components[i].state == SYS_COMPONENT_SKIPPED) {
			out_status->failed_component_count++;
		}
	}
	out_status->service_count = sys_service_get_status(NULL, 0U);
	out_status->active_thread_count = sys_thread_active_count();
	out_status->unjoined_thread_count = sys_thread_unjoined_count();
	sys_event_get_stats(&out_status->event_stats);
}
