#include <stdbool.h>

#include "camera_mod.h"
#include "test_common.h"

static int g_stop_event_count;

static sys_err_t reject_stream(const sys_event_t *event, void *private_data)
{
	(void)private_data;
	TEST_CHECK(event->id == CAMERA_EVENT_STREAM_STARTED);
	TEST_CHECK(event->size == sizeof(camera_stream_event_t));
	return SYS_ERR_GENERIC;
}

static sys_err_t observe_stream_stop(const sys_event_t *event, void *private_data)
{
	(void)private_data;
	TEST_CHECK(event->id == CAMERA_EVENT_STREAM_STOPPING);
	TEST_CHECK(event->size == sizeof(camera_stream_event_t));
	g_stop_event_count++;
	return SYS_OK;
}

int main(void)
{
	sys_event_subscription_t *start_subscription = NULL;
	sys_event_subscription_t *stop_subscription = NULL;
	bool is_streaming = true;

	TEST_CHECK(sys_core_init() == SYS_OK);
	TEST_CHECK(sys_event_subscribe(CAMERA_EVENT_STREAM_STARTED, reject_stream, NULL, &start_subscription) ==
		   SYS_OK);
	TEST_CHECK(sys_event_subscribe(CAMERA_EVENT_STREAM_STOPPING, observe_stream_stop, NULL, &stop_subscription) ==
		   SYS_OK);
	TEST_CHECK(camera_start_stream(30U, 320U, 240U) == SYS_ERR_GENERIC);
	TEST_CHECK(camera_is_streaming(&is_streaming) == SYS_OK);
	TEST_CHECK(!is_streaming);
	TEST_CHECK(g_stop_event_count == 1);
	TEST_CHECK(sys_event_unsubscribe(&stop_subscription) == SYS_OK);
	TEST_CHECK(sys_event_unsubscribe(&start_subscription) == SYS_OK);
	TEST_CHECK(sys_core_shutdown() == SYS_OK);
	return 0;
}
