/**
 * @file camera_mod.c
 * @brief Camera 数据生产者示例。
 */

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

#include "camera_mod.h"

#define CAMERA_MAX_FRAME_SIZE (64U * 1024U * 1024U)
#define CAMERA_MAX_BUFFER_COUNT 64U
#define CAMERA_MAX_FPS 240U

typedef enum {
	CAMERA_STATE_STOPPED = 0,
	CAMERA_STATE_STARTING,
	CAMERA_STATE_RUNNING,
	CAMERA_STATE_STOPPING,
	CAMERA_STATE_ERROR
} camera_state_t;

static pthread_mutex_t g_camera_mutex = PTHREAD_MUTEX_INITIALIZER;
static camera_state_t g_camera_state = CAMERA_STATE_STOPPED;
static atomic_bool g_camera_streaming;
static sys_thread_t *g_camera_thread;
static sys_buffer_pool_t *g_camera_pool;
static sys_channel_t *g_camera_channel;
static camera_stream_config_t g_camera_config;
static uint64_t g_frame_sequence;
static uint64_t g_dropped_frames;
static uint64_t g_stream_generation;

static void sleep_milliseconds(unsigned int milliseconds)
{
	struct timespec delay = {.tv_sec = (time_t)(milliseconds / 1000U),
				 .tv_nsec = (long)(milliseconds % 1000U) * 1000000L};

	while (nanosleep(&delay, &delay) != 0) {
	}
}

static uint64_t monotonic_microseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

static void *camera_producer(sys_thread_t *thread, void *argument)
{
	const unsigned int frame_interval_ms = 1000U / g_camera_config.fps;
	(void)argument;

	while (!sys_thread_stop_requested(thread)) {
		sys_buffer_t *buffer;
		sys_err_t ret = sys_buffer_acquire(g_camera_pool, &buffer);

		if (ret != SYS_OK) {
			g_dropped_frames++;
			sleep_milliseconds(frame_interval_ms);
			continue;
		}

		int length = snprintf(sys_buffer_data(buffer), sys_buffer_capacity(buffer), "CAMERA_FRAME_%06llu",
				      (unsigned long long)g_frame_sequence);
		if (length < 0 || (size_t)length >= sys_buffer_capacity(buffer)) {
			sys_buffer_release(buffer);
			break;
		}
		ret = sys_buffer_set_size(buffer, (size_t)length + 1U);
		if (ret != SYS_OK) {
			sys_buffer_release(buffer);
			break;
		}
		sys_buffer_set_sequence(buffer, g_frame_sequence++);
		sys_buffer_set_timestamp(buffer, monotonic_microseconds());
		sys_buffer_set_contract(buffer, CAMERA_BUFFER_TYPE_VIDEO_FRAME, CAMERA_BUFFER_FORMAT_VERSION,
					g_stream_generation);

		ret = sys_channel_send(g_camera_channel, buffer, 0);
		if (ret != SYS_OK && ret != SYS_ERR_CLOSED) {
			g_dropped_frames++;
		}
		sys_buffer_release(buffer);
		if (ret == SYS_ERR_CLOSED) {
			break;
		}
		sleep_milliseconds(frame_interval_ms);
	}
	return NULL;
}

static sys_err_t calculate_frame_capacity(const camera_stream_config_t *config, size_t *out_capacity)
{
	size_t pixels;

	if (config == NULL || out_capacity == NULL || config->fps == 0U || config->fps > CAMERA_MAX_FPS ||
	    config->width == 0U || config->height == 0U || config->buffer_count < 2U ||
	    config->buffer_count > CAMERA_MAX_BUFFER_COUNT || config->width > SIZE_MAX / config->height) {
		return SYS_ERR_INVALID_PARAM;
	}
	pixels = (size_t)config->width * config->height;
	if (pixels > SIZE_MAX / 3U) {
		return SYS_ERR_INVALID_PARAM;
	}
	*out_capacity = pixels * 3U / 2U;
	if (*out_capacity == 0U || *out_capacity > CAMERA_MAX_FRAME_SIZE) {
		return SYS_ERR_INVALID_PARAM;
	}
	return SYS_OK;
}

static sys_err_t cleanup_stream_resources(void)
{
	sys_err_t ret = SYS_OK;

	if (g_camera_channel != NULL) {
		sys_channel_release(g_camera_channel);
		g_camera_channel = NULL;
	}
	if (g_camera_pool != NULL) {
		ret = sys_buffer_pool_wait_idle(g_camera_pool, 2000);
		if (ret == SYS_OK) {
			ret = sys_buffer_pool_destroy(&g_camera_pool);
		}
	}
	return ret;
}

static sys_err_t create_stream_resources(const camera_stream_config_t *config, size_t frame_capacity)
{
	sys_err_t ret = sys_buffer_pool_create(config->buffer_count, frame_capacity, &g_camera_pool);

	if (ret == SYS_OK) {
		ret = sys_channel_create(config->buffer_count, &g_camera_channel);
	}
	if (ret != SYS_OK) {
		sys_err_t cleanup_ret = cleanup_stream_resources();

		if (cleanup_ret != SYS_OK) {
			SYS_LOG_ERROR_MSG("camera", "stream resource rollback failed: %s",
					  sys_error_string(cleanup_ret));
		}
		return ret;
	}

	g_camera_config = *config;
	g_frame_sequence = 0U;
	g_dropped_frames = 0U;
	g_stream_generation++;
	atomic_store_explicit(&g_camera_streaming, true, memory_order_release);
	ret = sys_thread_create("camera-producer", camera_producer, NULL, &g_camera_thread);
	if (ret != SYS_OK) {
		atomic_store_explicit(&g_camera_streaming, false, memory_order_release);
		sys_err_t cleanup_ret = cleanup_stream_resources();

		if (cleanup_ret != SYS_OK) {
			SYS_LOG_ERROR_MSG("camera", "thread rollback failed: %s", sys_error_string(cleanup_ret));
		}
		return ret;
	}
	return SYS_OK;
}

static void rollback_stream_start(const camera_stream_event_t *event)
{
	sys_err_t cleanup_ret = SYS_OK;
	sys_err_t event_ret = SYS_OK;
	sys_err_t join_ret;

	pthread_mutex_lock(&g_camera_mutex);
	g_camera_state = CAMERA_STATE_STOPPING;
	pthread_mutex_unlock(&g_camera_mutex);
	atomic_store_explicit(&g_camera_streaming, false, memory_order_release);
	sys_thread_request_stop(g_camera_thread);
	sys_channel_close(g_camera_channel);
	join_ret = sys_thread_join(&g_camera_thread, -1);
	if (join_ret == SYS_OK) {
		event_ret = sys_event_publish_sync(CAMERA_EVENT_STREAM_STOPPING, event, sizeof(*event));
		cleanup_ret = cleanup_stream_resources();
	}
	if (join_ret != SYS_OK) {
		SYS_LOG_ERROR_MSG("camera", "producer rollback failed: %s", sys_error_string(join_ret));
	}
	if (event_ret != SYS_OK) {
		SYS_LOG_ERROR_MSG("camera", "consumer rollback failed: %s", sys_error_string(event_ret));
	}
	if (cleanup_ret != SYS_OK) {
		SYS_LOG_ERROR_MSG("camera", "resource rollback failed: %s", sys_error_string(cleanup_ret));
	}
	pthread_mutex_lock(&g_camera_mutex);
	g_camera_state = g_camera_pool == NULL && g_camera_thread == NULL ? CAMERA_STATE_STOPPED : CAMERA_STATE_ERROR;
	pthread_mutex_unlock(&g_camera_mutex);
}

static sys_err_t camera_real_start(const camera_stream_config_t *config)
{
	camera_stream_event_t event;
	size_t frame_capacity;
	sys_err_t ret;

	ret = calculate_frame_capacity(config, &frame_capacity);
	if (ret != SYS_OK) {
		return ret;
	}
	pthread_mutex_lock(&g_camera_mutex);
	if (g_camera_state != CAMERA_STATE_STOPPED || g_camera_pool != NULL) {
		pthread_mutex_unlock(&g_camera_mutex);
		return SYS_ERR_STATE;
	}
	g_camera_state = CAMERA_STATE_STARTING;
	ret = create_stream_resources(config, frame_capacity);
	if (ret != SYS_OK) {
		g_camera_state = CAMERA_STATE_STOPPED;
		pthread_mutex_unlock(&g_camera_mutex);
		return ret;
	}

	event = (camera_stream_event_t){.abi_version = CAMERA_STREAM_EVENT_ABI_VERSION,
					.pixel_format = CAMERA_PIXEL_FORMAT_MOCK_YUV420,
					.generation = g_stream_generation,
					.channel = g_camera_channel,
					.config = g_camera_config};
	pthread_mutex_unlock(&g_camera_mutex);
	ret = sys_event_publish_sync(CAMERA_EVENT_STREAM_STARTED, &event, sizeof(event));
	if (ret != SYS_OK) {
		rollback_stream_start(&event);
		return ret;
	}

	pthread_mutex_lock(&g_camera_mutex);
	g_camera_state = CAMERA_STATE_RUNNING;
	pthread_mutex_unlock(&g_camera_mutex);
	SYS_LOG_INFO_MSG("camera", "stream started: %ux%u @ %u fps, %zu buffers", config->width, config->height,
			 config->fps, config->buffer_count);
	return SYS_OK;
}

static sys_err_t camera_real_stop(void)
{
	camera_stream_event_t event;
	sys_err_t event_ret;
	sys_err_t thread_ret;
	sys_err_t pool_ret = SYS_OK;

	pthread_mutex_lock(&g_camera_mutex);
	if (g_camera_state == CAMERA_STATE_STOPPED) {
		pthread_mutex_unlock(&g_camera_mutex);
		return SYS_OK;
	}
	if (g_camera_state != CAMERA_STATE_RUNNING || g_camera_pool == NULL || g_camera_channel == NULL) {
		pthread_mutex_unlock(&g_camera_mutex);
		return SYS_ERR_BUSY;
	}
	g_camera_state = CAMERA_STATE_STOPPING;
	event = (camera_stream_event_t){.abi_version = CAMERA_STREAM_EVENT_ABI_VERSION,
					.pixel_format = CAMERA_PIXEL_FORMAT_MOCK_YUV420,
					.generation = g_stream_generation,
					.channel = g_camera_channel,
					.config = g_camera_config};
	pthread_mutex_unlock(&g_camera_mutex);

	atomic_store_explicit(&g_camera_streaming, false, memory_order_release);
	sys_thread_request_stop(g_camera_thread);
	sys_channel_close(g_camera_channel);
	thread_ret = sys_thread_join(&g_camera_thread, -1);
	if (thread_ret != SYS_OK) {
		pthread_mutex_lock(&g_camera_mutex);
		g_camera_state = CAMERA_STATE_ERROR;
		pthread_mutex_unlock(&g_camera_mutex);
		return thread_ret;
	}

	event_ret = sys_event_publish_sync(CAMERA_EVENT_STREAM_STOPPING, &event, sizeof(event));
	sys_channel_release(g_camera_channel);
	g_camera_channel = NULL;
	pool_ret = sys_buffer_pool_wait_idle(g_camera_pool, 2000);
	if (pool_ret == SYS_OK) {
		pool_ret = sys_buffer_pool_destroy(&g_camera_pool);
	}
	SYS_LOG_INFO_MSG("camera", "stream stopped, dropped frames: %llu", (unsigned long long)g_dropped_frames);
	pthread_mutex_lock(&g_camera_mutex);
	g_camera_state = g_camera_pool == NULL ? CAMERA_STATE_STOPPED : CAMERA_STATE_ERROR;
	pthread_mutex_unlock(&g_camera_mutex);
	return event_ret != SYS_OK ? event_ret : pool_ret;
}

static bool camera_real_is_streaming(void)
{
	return atomic_load_explicit(&g_camera_streaming, memory_order_acquire);
}

static const camera_ops_t g_camera_ops = {
	.start_stream = camera_real_start, .stop_stream = camera_real_stop, .is_streaming = camera_real_is_streaming};

static sys_err_t camera_init(void)
{
	const sys_service_desc_t service = {.module_id = SYS_MOD_CAMERA,
					    .interface_id = CAMERA_INTERFACE_CONTROL,
					    .abi_version = CAMERA_ABI_VERSION,
					    .ops_size = sizeof(g_camera_ops),
					    .ops = &g_camera_ops,
					    .name = "camera.control"};

	atomic_init(&g_camera_streaming, false);
	g_camera_state = CAMERA_STATE_STOPPED;
	return sys_service_register(&service);
}

static sys_err_t camera_component_stop(void)
{
	return camera_real_stop();
}

static void camera_deinit(void)
{
	sys_err_t ret = sys_service_unregister(SYS_MOD_CAMERA, CAMERA_INTERFACE_CONTROL);

	if (ret != SYS_OK) {
		SYS_LOG_ERROR_MSG("camera", "service unregister failed: %s", sys_error_string(ret));
	}
}

SYS_COMPONENT_REGISTER(g_camera_component, .id = SYS_MOD_CAMERA, .name = "camera", .phase = SYS_COMPONENT_PHASE_SERVICE,
		       .policy = SYS_COMPONENT_REQUIRED, .init = camera_init, .stop = camera_component_stop,
		       .deinit = camera_deinit);
