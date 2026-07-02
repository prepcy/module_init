#ifndef CAMERA_MOD_H
#define CAMERA_MOD_H

#include "sys_core.h"
#include "app_modules.h"

typedef struct {
	int (*start_stream)(int fps, int resolution_width, int resolution_height);
	void *(*get_frame_buffer)(void);
	void (*stop_stream)(void);
} camera_ops_t;

// 统一安全代理 API (内联展开，零开销防御段错误)
static inline sys_err_t camera_start_stream(int fps, int resolution_width, int resolution_height)
{
	camera_ops_t *ops = (camera_ops_t *)sys_subsystem_get(SYS_MOD_CAMERA);
	if (!ops) {
		return SYS_ERR_NOT_FOUND;
	}
	if (!ops->start_stream) {
		return SYS_ERR_NOT_SUPPORTED;
	}
	return ops->start_stream(fps, resolution_width, resolution_height);
}

static inline void *camera_get_frame_buffer(void)
{
	camera_ops_t *ops = (camera_ops_t *)sys_subsystem_get(SYS_MOD_CAMERA);
	if (ops && ops->get_frame_buffer) {
		return ops->get_frame_buffer();
	}
	return NULL; // 模块未加载或接口为空
}

static inline void camera_stop_stream(void)
{
	camera_ops_t *ops = (camera_ops_t *)sys_subsystem_get(SYS_MOD_CAMERA);
	if (ops && ops->stop_stream) {
		ops->stop_stream();
	}
}

#endif // CAMERA_MOD_H
