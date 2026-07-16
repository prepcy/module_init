/**
 * @file camera_mod.h
 * @brief Camera 控制接口和视频数据通道事件。
 */

#ifndef CAMERA_MOD_H
#define CAMERA_MOD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_modules.h"
#include "sys_core.h"

#define CAMERA_INTERFACE_CONTROL 1U
#define CAMERA_ABI_VERSION 1U

#define CAMERA_EVENT_STREAM_STARTED 0x04000001U
#define CAMERA_EVENT_STREAM_STOPPING 0x04000002U

typedef struct {
	unsigned int fps;
	unsigned int width;
	unsigned int height;
	size_t buffer_count;
} camera_stream_config_t;

/**
 * 事件回调若要在回调返回后继续使用 channel，必须调用 sys_channel_retain()。
 */
typedef struct {
	sys_channel_t *channel;
	camera_stream_config_t config;
} camera_stream_event_t;

typedef struct {
	sys_err_t (*start_stream)(const camera_stream_config_t *config);
	sys_err_t (*stop_stream)(void);
	bool (*is_streaming)(void);
} camera_ops_t;

/** @brief 使用指定帧率和分辨率启动视频流。 */
static inline sys_err_t camera_start_stream(unsigned int fps, unsigned int width, unsigned int height)
{
	sys_service_ref_t ref;
	camera_stream_config_t config = {.fps = fps, .width = width, .height = height, .buffer_count = 8U};
	sys_err_t ret;

	ret = sys_service_acquire(SYS_MOD_CAMERA, CAMERA_INTERFACE_CONTROL, CAMERA_ABI_VERSION, sizeof(camera_ops_t),
				  &ref);
	if (ret != SYS_OK) {
		return ret;
	}
	const camera_ops_t *ops = ref.ops;
	ret = ops->start_stream == NULL ? SYS_ERR_NOT_SUPPORTED : ops->start_stream(&config);
	sys_service_release(&ref);
	return ret;
}

/** @brief 停止视频流并完成消费者关闭握手。 */
static inline sys_err_t camera_stop_stream(void)
{
	sys_service_ref_t ref;
	sys_err_t ret;

	ret = sys_service_acquire(SYS_MOD_CAMERA, CAMERA_INTERFACE_CONTROL, CAMERA_ABI_VERSION, sizeof(camera_ops_t),
				  &ref);
	if (ret != SYS_OK) {
		return ret;
	}
	const camera_ops_t *ops = ref.ops;
	ret = ops->stop_stream == NULL ? SYS_ERR_NOT_SUPPORTED : ops->stop_stream();
	sys_service_release(&ref);
	return ret;
}

/** @brief 查询视频流是否运行。 */
static inline sys_err_t camera_is_streaming(bool *out_streaming)
{
	sys_service_ref_t ref;
	sys_err_t ret;

	if (out_streaming == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	ret = sys_service_acquire(SYS_MOD_CAMERA, CAMERA_INTERFACE_CONTROL, CAMERA_ABI_VERSION, sizeof(camera_ops_t),
				  &ref);
	if (ret != SYS_OK) {
		return ret;
	}
	const camera_ops_t *ops = ref.ops;
	if (ops->is_streaming == NULL) {
		ret = SYS_ERR_NOT_SUPPORTED;
	} else {
		*out_streaming = ops->is_streaming();
		ret = SYS_OK;
	}
	sys_service_release(&ref);
	return ret;
}

#endif
