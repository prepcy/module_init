/**
 * @file zlmedia_mod.c
 * @brief 视频数据通道消费者示例。
 */

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#include "camera_mod.h"
#include "zlmedia_mod.h"

static pthread_mutex_t g_zlmedia_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool g_zlmedia_running;
static pthread_t g_zlmedia_thread;
static bool g_zlmedia_thread_started;
static sys_channel_t *g_zlmedia_channel;
static unsigned int g_zlmedia_port = 8554U;
static uint64_t g_consumed_frames;
static sys_event_subscription_t *g_start_subscription;
static sys_event_subscription_t *g_stop_subscription;

static void *zlmedia_consumer(void *argument)
{
	sys_channel_t *channel = argument;

	while (atomic_load_explicit(&g_zlmedia_running, memory_order_acquire)) {
		sys_buffer_t *buffer;
		sys_err_t ret = sys_channel_receive(channel, &buffer, 200);

		if (ret == SYS_ERR_TIMEOUT) {
			continue;
		}
		if (ret == SYS_ERR_CLOSED) {
			break;
		}
		if (ret != SYS_OK) {
			break;
		}

		g_consumed_frames++;
		if (g_consumed_frames == 1U || g_consumed_frames % 30U == 0U) {
			printf("[ZLMedia] frame=%llu size=%zu timestamp=%llu payload=%s\n",
			       (unsigned long long)sys_buffer_sequence(buffer), sys_buffer_size(buffer),
			       (unsigned long long)sys_buffer_timestamp(buffer), (char *)sys_buffer_data(buffer));
		}
		sys_buffer_release(buffer);
	}
	return NULL;
}

static void detach_stream(void)
{
	pthread_t thread;
	sys_channel_t *channel;
	bool should_join;

	pthread_mutex_lock(&g_zlmedia_mutex);
	atomic_store_explicit(&g_zlmedia_running, false, memory_order_release);
	thread = g_zlmedia_thread;
	channel = g_zlmedia_channel;
	should_join = g_zlmedia_thread_started;
	g_zlmedia_thread_started = false;
	g_zlmedia_channel = NULL;
	pthread_mutex_unlock(&g_zlmedia_mutex);

	if (should_join) {
		pthread_join(thread, NULL);
	}
	if (channel != NULL) {
		sys_channel_release(channel);
	}
}

static sys_err_t on_stream_started(const sys_event_t *event, void *private_data)
{
	const camera_stream_event_t *stream_event;
	sys_err_t ret;
	unsigned int port;
	(void)private_data;

	if (event->size != sizeof(camera_stream_event_t) || event->data == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	stream_event = event->data;

	pthread_mutex_lock(&g_zlmedia_mutex);
	if (g_zlmedia_thread_started) {
		pthread_mutex_unlock(&g_zlmedia_mutex);
		return SYS_ERR_STATE;
	}
	ret = sys_channel_retain(stream_event->channel);
	if (ret != SYS_OK) {
		pthread_mutex_unlock(&g_zlmedia_mutex);
		return ret;
	}
	g_zlmedia_channel = stream_event->channel;
	g_consumed_frames = 0U;
	atomic_store_explicit(&g_zlmedia_running, true, memory_order_release);
	if (pthread_create(&g_zlmedia_thread, NULL, zlmedia_consumer, g_zlmedia_channel) != 0) {
		atomic_store_explicit(&g_zlmedia_running, false, memory_order_release);
		sys_channel_release(g_zlmedia_channel);
		g_zlmedia_channel = NULL;
		pthread_mutex_unlock(&g_zlmedia_mutex);
		return SYS_ERR_GENERIC;
	}
	g_zlmedia_thread_started = true;
	port = g_zlmedia_port;
	pthread_mutex_unlock(&g_zlmedia_mutex);
	printf("[ZLMedia] attached camera stream on port %u\n", port);
	return SYS_OK;
}

static sys_err_t on_stream_stopping(const sys_event_t *event, void *private_data)
{
	(void)private_data;
	if (event->size != sizeof(camera_stream_event_t) || event->data == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	detach_stream();
	printf("[ZLMedia] detached camera stream, consumed frames: %llu\n", (unsigned long long)g_consumed_frames);
	return SYS_OK;
}

static sys_err_t zlmedia_real_set_port(unsigned int port)
{
	if (port == 0U || port > 65535U) {
		return SYS_ERR_INVALID_PARAM;
	}
	pthread_mutex_lock(&g_zlmedia_mutex);
	g_zlmedia_port = port;
	pthread_mutex_unlock(&g_zlmedia_mutex);
	return SYS_OK;
}

static sys_err_t zlmedia_real_get_streaming(bool *out_streaming)
{
	*out_streaming = atomic_load_explicit(&g_zlmedia_running, memory_order_acquire);
	return SYS_OK;
}

static const zlmedia_ops_t g_zlmedia_ops = {.set_port = zlmedia_real_set_port,
					    .get_streaming = zlmedia_real_get_streaming};

static sys_err_t zlmedia_init(void)
{
	const sys_service_desc_t service = {.module_id = SYS_MOD_ZLMEDIA,
					    .interface_id = ZLMEDIA_INTERFACE_CONTROL,
					    .abi_version = ZLMEDIA_ABI_VERSION,
					    .ops_size = sizeof(g_zlmedia_ops),
					    .ops = &g_zlmedia_ops,
					    .name = "zlmedia.control"};
	sys_err_t ret;

	atomic_init(&g_zlmedia_running, false);
	ret = sys_event_subscribe(CAMERA_EVENT_STREAM_STARTED, on_stream_started, NULL, &g_start_subscription);
	if (ret == SYS_OK) {
		ret = sys_event_subscribe(CAMERA_EVENT_STREAM_STOPPING, on_stream_stopping, NULL, &g_stop_subscription);
	}
	if (ret == SYS_OK) {
		ret = sys_service_register(&service);
	}
	if (ret != SYS_OK) {
		if (g_stop_subscription != NULL) {
			(void)sys_event_unsubscribe(&g_stop_subscription);
		}
		if (g_start_subscription != NULL) {
			(void)sys_event_unsubscribe(&g_start_subscription);
		}
	}
	return ret;
}

static void zlmedia_exit(void)
{
	(void)sys_event_unsubscribe(&g_stop_subscription);
	(void)sys_event_unsubscribe(&g_start_subscription);
	detach_stream();
	(void)sys_service_unregister(SYS_MOD_ZLMEDIA, ZLMEDIA_INTERFACE_CONTROL);
}

static const uint32_t g_zlmedia_dependencies[] = {SYS_MOD_CAMERA};

SYS_COMPONENT_REGISTER(g_zlmedia_component, SYS_MOD_ZLMEDIA, "zlmedia", SYS_COMPONENT_PHASE_SERVICE,
		       g_zlmedia_dependencies, SYS_ARRAY_SIZE(g_zlmedia_dependencies), zlmedia_init, zlmedia_exit);
