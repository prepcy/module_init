/**
 * @file sys_types.h
 * @brief 框架公共基础类型。
 */

#ifndef SYS_TYPES_H
#define SYS_TYPES_H

#include <stddef.h>

typedef enum {
	SYS_OK = 0,
	SYS_ERR_GENERIC = -1,
	SYS_ERR_NOT_FOUND = -2,
	SYS_ERR_NOT_SUPPORTED = -3,
	SYS_ERR_INVALID_PARAM = -4,
	SYS_ERR_NO_MEMORY = -5,
	SYS_ERR_ALREADY_EXISTS = -6,
	SYS_ERR_BUSY = -7,
	SYS_ERR_TIMEOUT = -8,
	SYS_ERR_QUEUE_FULL = -9,
	SYS_ERR_CLOSED = -10,
	SYS_ERR_DEPENDENCY = -11,
	SYS_ERR_STATE = -12,
	SYS_ERR_ABI_MISMATCH = -13
} sys_err_t;

#define SYS_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#endif
