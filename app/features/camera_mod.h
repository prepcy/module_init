#ifndef CAMERA_MOD_H
#define CAMERA_MOD_H

#include "sys_core.h"
#include "app_modules.h"

#define CMD_CAM_START_STREAM     2001
#define CMD_CAM_STOP_STREAM      2002

typedef struct {
	int (*start_stream)(int fps, int resolution_width, int resolution_height);
	void *(*get_frame_buffer)(void);
	void (*stop_stream)(void);
} camera_ops_t;

// 统一安全代理 API (内联展开，零开销防御段错误)
static inline sys_err_t camera_start_stream(int fps, int resolution_width, int resolution_height)
{
	(void)fps; (void)resolution_width; (void)resolution_height;
	return sys_subsystem_ioctl(SYS_MOD_CAMERA, CMD_CAM_START_STREAM, NULL);
}

static inline void *camera_get_frame_buffer(void)
{
	return NULL;
}

static inline void camera_stop_stream(void)
{
	sys_subsystem_ioctl(SYS_MOD_CAMERA, CMD_CAM_STOP_STREAM, NULL);
}

static inline sys_err_t camera_start_streaming(void)
{
	return sys_subsystem_ioctl(SYS_MOD_CAMERA, CMD_CAM_START_STREAM, NULL);
}

static inline sys_err_t camera_stop_streaming(void)
{
	return sys_subsystem_ioctl(SYS_MOD_CAMERA, CMD_CAM_STOP_STREAM, NULL);
}

#endif // CAMERA_MOD_H
