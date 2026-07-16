/**
 * @file zlmedia_mod.h
 * @brief 流媒体消费者控制接口。
 */

#ifndef ZLMEDIA_MOD_H
#define ZLMEDIA_MOD_H

#include <stdbool.h>

#include "app_modules.h"
#include "sys_core.h"

#define ZLMEDIA_INTERFACE_CONTROL 1U
#define ZLMEDIA_ABI_VERSION 1U

typedef struct {
	sys_err_t (*set_port)(unsigned int port);
	sys_err_t (*get_streaming)(bool *out_streaming);
} zlmedia_ops_t;

/** @brief 设置流媒体监听端口。 */
static inline sys_err_t zlmedia_set_port(unsigned int port)
{
	sys_service_ref_t ref;
	sys_err_t ret;

	ret = sys_service_acquire(SYS_MOD_ZLMEDIA, ZLMEDIA_INTERFACE_CONTROL, ZLMEDIA_ABI_VERSION,
				  sizeof(zlmedia_ops_t), &ref);
	if (ret != SYS_OK) {
		return ret;
	}
	const zlmedia_ops_t *ops = ref.ops;
	ret = ops->set_port == NULL ? SYS_ERR_NOT_SUPPORTED : ops->set_port(port);
	sys_service_release(&ref);
	return ret;
}

/** @brief 查询流媒体消费者是否正在处理数据。 */
static inline sys_err_t zlmedia_get_streaming(bool *out_streaming)
{
	sys_service_ref_t ref;
	sys_err_t ret;

	if (out_streaming == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	ret = sys_service_acquire(SYS_MOD_ZLMEDIA, ZLMEDIA_INTERFACE_CONTROL, ZLMEDIA_ABI_VERSION,
				  sizeof(zlmedia_ops_t), &ref);
	if (ret != SYS_OK) {
		return ret;
	}
	const zlmedia_ops_t *ops = ref.ops;
	ret = ops->get_streaming == NULL ? SYS_ERR_NOT_SUPPORTED : ops->get_streaming(out_streaming);
	sys_service_release(&ref);
	return ret;
}

#endif
