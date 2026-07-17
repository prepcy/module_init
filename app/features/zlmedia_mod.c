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
static sys_thread_t *g_zlmedia_thread;
static sys_channel_t *g_zlmedia_channel;
static unsigned int g_zlmedia_port = 8554U;
static uint64_t g_consumed_frames;
static uint64_t g_stream_generation;
static sys_event_subscription_t *g_start_subscription;
static sys_event_subscription_t *g_stop_subscription;

static void *zlmedia_consumer(sys_thread_t *thread, void *argument)
{
	sys_channel_t *channel = argument;

	while (!sys_thread_stop_requested(thread)) {
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
		if (sys_buffer_type(buffer) != CAMERA_BUFFER_TYPE_VIDEO_FRAME ||
		    sys_buffer_version(buffer) != CAMERA_BUFFER_FORMAT_VERSION ||
		    sys_buffer_generation(buffer) != g_stream_generation) {
			sys_buffer_release(buffer);
			continue;
		}

		g_consumed_frames++;
		if (g_consumed_frames == 1U || g_consumed_frames % 30U == 0U) {
			SYS_LOG_INFO_MSG("zlmedia", "frame=%llu size=%zu timestamp=%llu payload=%s",
					 (unsigned long long)sys_buffer_sequence(buffer), sys_buffer_size(buffer),
					 (unsigned long long)sys_buffer_timestamp(buffer),
					 (char *)sys_buffer_data(buffer));
		}
		sys_buffer_release(buffer);
	}
	return NULL;
}

static sys_err_t detach_stream(void)
{
	sys_thread_t *thread;
	sys_channel_t *channel;

	pthread_mutex_lock(&g_zlmedia_mutex);
	atomic_store_explicit(&g_zlmedia_running, false, memory_order_release);
	thread = g_zlmedia_thread;
	g_zlmedia_thread = NULL;
	channel = g_zlmedia_channel;
	g_zlmedia_channel = NULL;
	pthread_mutex_unlock(&g_zlmedia_mutex);

	if (thread != NULL) {
		sys_thread_request_stop(thread);
		sys_err_t ret = sys_thread_join(&thread, -1);

		if (ret != SYS_OK) {
			pthread_mutex_lock(&g_zlmedia_mutex);
			g_zlmedia_thread = thread;
			g_zlmedia_channel = channel;
			pthread_mutex_unlock(&g_zlmedia_mutex);
			return ret;
		}
	}
	if (channel != NULL) {
		sys_channel_release(channel);
	}
	return SYS_OK;
}

static sys_err_t on_stream_started(const sys_event_t *event, void *private_data)
{
	const camera_stream_event_t *stream_event;
	sys_err_t ret;
	unsigned int port;
	(void)private_data;

	if (event->abi_version != SYS_EVENT_ABI_VERSION || event->size != sizeof(camera_stream_event_t) ||
	    event->data == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	stream_event = event->data;
	if (stream_event->abi_version != CAMERA_STREAM_EVENT_ABI_VERSION) {
		return SYS_ERR_ABI_MISMATCH;
	}

	pthread_mutex_lock(&g_zlmedia_mutex);
	if (g_zlmedia_thread != NULL) {
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
	g_stream_generation = stream_event->generation;
	atomic_store_explicit(&g_zlmedia_running, true, memory_order_release);
	ret = sys_thread_create("zlmedia-rx", zlmedia_consumer, g_zlmedia_channel, &g_zlmedia_thread);
	if (ret != SYS_OK) {
		atomic_store_explicit(&g_zlmedia_running, false, memory_order_release);
		sys_channel_release(g_zlmedia_channel);
		g_zlmedia_channel = NULL;
		pthread_mutex_unlock(&g_zlmedia_mutex);
		return ret;
	}
	port = g_zlmedia_port;
	pthread_mutex_unlock(&g_zlmedia_mutex);
	SYS_LOG_INFO_MSG("zlmedia", "attached camera stream on port %u", port);
	return SYS_OK;
}

static sys_err_t on_stream_stopping(const sys_event_t *event, void *private_data)
{
	(void)private_data;
	if (event->abi_version != SYS_EVENT_ABI_VERSION || event->size != sizeof(camera_stream_event_t) ||
	    event->data == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	sys_err_t ret = detach_stream();
	if (ret != SYS_OK) {
		return ret;
	}
	SYS_LOG_INFO_MSG("zlmedia", "detached camera stream, consumed frames: %llu",
			 (unsigned long long)g_consumed_frames);
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

static void log_deinit_error(const char *operation, sys_err_t error)
{
	if (error != SYS_OK) {
		SYS_LOG_ERROR_MSG("zlmedia", "%s failed: %s", operation, sys_error_string(error));
	}
}

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
			log_deinit_error("stop-event rollback", sys_event_unsubscribe(&g_stop_subscription));
		}
		if (g_start_subscription != NULL) {
			log_deinit_error("start-event rollback", sys_event_unsubscribe(&g_start_subscription));
		}
	}
	return ret;
}

static sys_err_t zlmedia_stop(void)
{
	return detach_stream();
}

static void zlmedia_deinit(void)
{
	log_deinit_error("stop-event unsubscribe", sys_event_unsubscribe(&g_stop_subscription));
	log_deinit_error("start-event unsubscribe", sys_event_unsubscribe(&g_start_subscription));
	log_deinit_error("service unregister", sys_service_unregister(SYS_MOD_ZLMEDIA, ZLMEDIA_INTERFACE_CONTROL));
}

static const uint32_t g_zlmedia_dependencies[] = {SYS_MOD_CAMERA};

SYS_COMPONENT_REGISTER(g_zlmedia_component, .id = SYS_MOD_ZLMEDIA, .name = "zlmedia",
		       .phase = SYS_COMPONENT_PHASE_SERVICE, .policy = SYS_COMPONENT_OPTIONAL,
		       .dependencies = g_zlmedia_dependencies,
		       .dependency_count = SYS_ARRAY_SIZE(g_zlmedia_dependencies), .init = zlmedia_init,
		       .stop = zlmedia_stop, .deinit = zlmedia_deinit);
