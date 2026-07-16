/**
 * @file sys_service.h
 * @brief 带类型和 ABI 校验的模块服务注册中心。
 */

#ifndef SYS_SERVICE_H
#define SYS_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "sys_types.h"

typedef struct {
	uint32_t module_id;
	uint32_t interface_id;
	uint32_t abi_version;
	size_t ops_size;
	const void *ops;
	const char *name;
} sys_service_desc_t;

typedef struct {
	const void *ops;
	void *internal;
} sys_service_ref_t;

/**
 * @brief 注册一个模块接口。
 * @param desc 静态生命周期的服务描述符。
 * @return SYS_OK 表示成功；重复键返回 SYS_ERR_ALREADY_EXISTS。
 */
sys_err_t sys_service_register(const sys_service_desc_t *desc);

/**
 * @brief 注销接口并等待所有在途调用释放引用。
 */
sys_err_t sys_service_unregister(uint32_t module_id, uint32_t interface_id);

/**
 * @brief 获取经过 ABI 和结构大小校验的服务引用。
 */
sys_err_t sys_service_acquire(uint32_t module_id, uint32_t interface_id, uint32_t abi_version, size_t minimum_ops_size,
			      sys_service_ref_t *out_ref);

/**
 * @brief 释放服务引用。
 */
void sys_service_release(sys_service_ref_t *ref);

#endif
