#include <pthread.h>
#include <stdatomic.h>

#include "sys_core.h"
#include "test_common.h"

#define TEST_EVENT_ID 0x99000001U

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static bool g_callback_entered;
static bool g_callback_released;
static atomic_bool g_shutdown_done;

static sys_err_t blocking_callback(const sys_event_t *event, void *private_data)
{
	(void)event;
	(void)private_data;
	pthread_mutex_lock(&g_mutex);
	g_callback_entered = true;
	pthread_cond_broadcast(&g_cond);
	while (!g_callback_released) {
		pthread_cond_wait(&g_cond, &g_mutex);
	}
	pthread_mutex_unlock(&g_mutex);
	return SYS_OK;
}

static void *publish_event(void *argument)
{
	sys_err_t *result = argument;

	*result = sys_event_publish_sync(TEST_EVENT_ID, NULL, 0U);
	return NULL;
}

static void *shutdown_core(void *argument)
{
	sys_err_t *result = argument;

	*result = sys_core_shutdown();
	atomic_store_explicit(&g_shutdown_done, true, memory_order_release);
	return NULL;
}

int main(void)
{
	sys_event_subscription_t *subscription = NULL;
	pthread_t publisher;
	pthread_t shutdown_thread;
	sys_err_t publish_result = SYS_ERR_GENERIC;
	sys_err_t shutdown_result = SYS_ERR_GENERIC;

	atomic_init(&g_shutdown_done, false);
	TEST_CHECK(sys_core_init() == SYS_OK);
	TEST_CHECK(sys_event_subscribe(TEST_EVENT_ID, blocking_callback, NULL, &subscription) == SYS_OK);
	TEST_CHECK(pthread_create(&publisher, NULL, publish_event, &publish_result) == 0);
	pthread_mutex_lock(&g_mutex);
	while (!g_callback_entered) {
		pthread_cond_wait(&g_cond, &g_mutex);
	}
	pthread_mutex_unlock(&g_mutex);
	TEST_CHECK(pthread_create(&shutdown_thread, NULL, shutdown_core, &shutdown_result) == 0);
	TEST_CHECK(!atomic_load_explicit(&g_shutdown_done, memory_order_acquire));
	pthread_mutex_lock(&g_mutex);
	g_callback_released = true;
	pthread_cond_broadcast(&g_cond);
	pthread_mutex_unlock(&g_mutex);
	TEST_CHECK(pthread_join(publisher, NULL) == 0);
	TEST_CHECK(pthread_join(shutdown_thread, NULL) == 0);
	TEST_CHECK(publish_result == SYS_OK && shutdown_result == SYS_OK);
	return 0;
}
