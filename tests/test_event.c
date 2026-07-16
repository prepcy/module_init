#include <stdatomic.h>

#include "sys_core.h"
#include "test_common.h"

#define TEST_EVENT_ID 0x90000001U

static atomic_int g_callback_count;
static atomic_int g_last_value;

static sys_err_t handle_event(const sys_event_t *event, void *private_data)
{
	const int *value = event->data;
	(void)private_data;

	TEST_CHECK(event->id == TEST_EVENT_ID);
	TEST_CHECK(event->size == sizeof(*value));
	atomic_store(&g_last_value, *value);
	atomic_fetch_add(&g_callback_count, 1);
	return SYS_OK;
}

int main(void)
{
	sys_event_subscription_t *subscription = NULL;
	int value = 10;

	atomic_init(&g_callback_count, 0);
	atomic_init(&g_last_value, 0);
	TEST_CHECK(sys_core_init() == SYS_OK);
	TEST_CHECK(sys_event_subscribe(TEST_EVENT_ID, handle_event, NULL, &subscription) == SYS_OK);
	TEST_CHECK(sys_event_publish_sync(TEST_EVENT_ID, &value, sizeof(value)) == SYS_OK);
	TEST_CHECK(atomic_load(&g_callback_count) == 1);

	value = 20;
	TEST_CHECK(sys_event_publish_async(TEST_EVENT_ID, &value, sizeof(value)) == SYS_OK);
	TEST_CHECK(sys_event_unsubscribe(&subscription) == SYS_OK);
	TEST_CHECK(atomic_load(&g_callback_count) == 2);
	TEST_CHECK(atomic_load(&g_last_value) == 20);
	sys_core_shutdown();
	return 0;
}
