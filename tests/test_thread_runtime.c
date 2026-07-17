#include <sched.h>
#include <stdatomic.h>

#include "sys_core.h"
#include "test_common.h"

static atomic_bool g_thread_started;

static void *cooperative_worker(sys_thread_t *thread, void *argument)
{
	(void)argument;
	atomic_store_explicit(&g_thread_started, true, memory_order_release);
	while (!sys_thread_stop_requested(thread)) {
		sched_yield();
	}
	return NULL;
}

static void test_managed_thread(void)
{
	sys_thread_t *thread = NULL;

	atomic_init(&g_thread_started, false);
	thread = (sys_thread_t *)(uintptr_t)1U;
	TEST_CHECK(sys_thread_create("name-is-too-long", cooperative_worker, NULL, &thread) == SYS_ERR_INVALID_PARAM);
	TEST_CHECK(thread == NULL);
	TEST_CHECK(sys_thread_create("worker", cooperative_worker, NULL, &thread) == SYS_OK);
	while (!atomic_load_explicit(&g_thread_started, memory_order_acquire)) {
		sched_yield();
	}
	TEST_CHECK(sys_thread_active_count() == 1U);
	TEST_CHECK(sys_thread_unjoined_count() == 1U);
	TEST_CHECK(sys_thread_join(&thread, 0) == SYS_ERR_TIMEOUT);
	sys_thread_request_stop(thread);
	TEST_CHECK(sys_thread_join(&thread, 1000) == SYS_OK);
	TEST_CHECK(sys_thread_active_count() == 0U);
	TEST_CHECK(sys_thread_unjoined_count() == 0U);
}

static void test_programmatic_runtime_stop(void)
{
	sys_runtime_t *runtime = NULL;

	TEST_CHECK(sys_runtime_create(&runtime) == SYS_OK);
	TEST_CHECK(sys_runtime_wait(runtime, 0) == SYS_ERR_TIMEOUT);
	TEST_CHECK(sys_runtime_request_stop(runtime) == SYS_OK);
	TEST_CHECK(sys_runtime_wait(runtime, 1000) == SYS_ERR_CANCELLED);
	TEST_CHECK(sys_runtime_destroy(&runtime) == SYS_OK);
}

int main(void)
{
	test_managed_thread();
	test_programmatic_runtime_stop();
	return 0;
}
