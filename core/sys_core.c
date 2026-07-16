/**
 * @file sys_core.c
 * @brief 框架总生命周期实现。
 */

#include <pthread.h>

#include "sys_core.h"
#include "sys_internal.h"

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
		sys_event_bus_stop();
		sys_service_registry_reset();
	}

	pthread_mutex_lock(&g_core_mutex);
	g_core_state = ret == SYS_OK ? SYS_CORE_RUNNING : SYS_CORE_STOPPED;
	pthread_mutex_unlock(&g_core_mutex);
	return ret;
}

void sys_core_shutdown(void)
{
	pthread_mutex_lock(&g_core_mutex);
	if (g_core_state != SYS_CORE_RUNNING) {
		pthread_mutex_unlock(&g_core_mutex);
		return;
	}
	g_core_state = SYS_CORE_STOPPING;
	pthread_mutex_unlock(&g_core_mutex);

	sys_component_stop_all();
	sys_event_bus_stop();
	sys_service_registry_reset();

	pthread_mutex_lock(&g_core_mutex);
	g_core_state = SYS_CORE_STOPPED;
	pthread_mutex_unlock(&g_core_mutex);
}

bool sys_core_is_running(void)
{
	bool is_running;

	pthread_mutex_lock(&g_core_mutex);
	is_running = g_core_state == SYS_CORE_RUNNING;
	pthread_mutex_unlock(&g_core_mutex);
	return is_running;
}
