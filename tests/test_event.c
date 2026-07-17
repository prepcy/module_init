#include <stdatomic.h>

#include "sys_core.h"
#include "test_common.h"

#define TEST_EVENT_ID 0x90000001U

static atomic_int g_callback_count;
static atomic_int g_last_value;

static sys_err_t reject_event(const sys_event_t *event, void *private_data)
{
	(void)event;
	(void)private_data;
	return SYS_ERR_GENERIC;
}

static sys_err_t handle_event(const sys_event_t *event, void *private_data)
{
	const int *value = event->data;
	(void)private_data;

	TEST_CHECK(event->id == TEST_EVENT_ID);
	TEST_CHECK(event->abi_version == SYS_EVENT_ABI_VERSION);
	TEST_CHECK(event->size == sizeof(*value));
	atomic_store(&g_last_value, *value);
	atomic_fetch_add(&g_callback_count, 1);
	return SYS_OK;
}

int main(void)
{
	sys_event_subscription_t *subscription = NULL;
	sys_event_subscription_t *rejecting_subscription = NULL;
	sys_event_stats_t stats;
	int value = 10;

	atomic_init(&g_callback_count, 0);
	atomic_init(&g_last_value, 0);
	subscription = (sys_event_subscription_t *)(uintptr_t)1U;
	TEST_CHECK(sys_event_subscribe(0U, handle_event, NULL, &subscription) == SYS_ERR_INVALID_PARAM);
	TEST_CHECK(subscription == NULL);
	TEST_CHECK(sys_core_init() == SYS_OK);
	TEST_CHECK(sys_event_subscribe(TEST_EVENT_ID, handle_event, NULL, &subscription) == SYS_OK);
	TEST_CHECK(sys_event_publish_sync(TEST_EVENT_ID, &value, sizeof(value)) == SYS_OK);
	TEST_CHECK(atomic_load(&g_callback_count) == 1);

	value = 20;
	TEST_CHECK(sys_event_publish_async(TEST_EVENT_ID, &value, sizeof(value)) == SYS_OK);
	TEST_CHECK(sys_event_unsubscribe(&subscription) == SYS_OK);
	TEST_CHECK(atomic_load(&g_callback_count) == 2);
	TEST_CHECK(atomic_load(&g_last_value) == 20);
	TEST_CHECK(sys_event_subscribe(TEST_EVENT_ID + 1U, reject_event, NULL, &rejecting_subscription) == SYS_OK);
	TEST_CHECK(sys_event_publish_sync(TEST_EVENT_ID + 1U, NULL, 0U) == SYS_ERR_GENERIC);
	TEST_CHECK(sys_event_unsubscribe(&rejecting_subscription) == SYS_OK);
	TEST_CHECK(sys_event_publish_async(TEST_EVENT_ID + 2U, NULL, 0U) == SYS_OK);
	sys_event_get_stats(&stats);
	TEST_CHECK(stats.synchronous_publish_count == 2U);
	TEST_CHECK(stats.asynchronous_publish_count == 2U);
	TEST_CHECK(stats.callback_failure_count == 1U);
	TEST_CHECK(stats.current_queue_depth == 0U && stats.queue_high_watermark == 1U);
	TEST_CHECK(sys_core_shutdown() == SYS_OK);
	return 0;
}
