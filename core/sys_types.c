/**
 * @file sys_types.c
 * @brief 框架错误码文本和 errno 映射。
 */

#include <errno.h>

#include "sys_types.h"

const char *sys_error_string(sys_err_t error)
{
	switch (error) {
	case SYS_OK:
		return "success";
	case SYS_ERR_GENERIC:
		return "generic failure";
	case SYS_ERR_NOT_FOUND:
		return "not found";
	case SYS_ERR_NOT_SUPPORTED:
		return "not supported";
	case SYS_ERR_INVALID_PARAM:
		return "invalid parameter";
	case SYS_ERR_NO_MEMORY:
		return "out of memory";
	case SYS_ERR_ALREADY_EXISTS:
		return "already exists";
	case SYS_ERR_BUSY:
		return "busy";
	case SYS_ERR_TIMEOUT:
		return "timeout";
	case SYS_ERR_QUEUE_FULL:
		return "queue full";
	case SYS_ERR_CLOSED:
		return "closed";
	case SYS_ERR_DEPENDENCY:
		return "dependency failure";
	case SYS_ERR_STATE:
		return "invalid state";
	case SYS_ERR_ABI_MISMATCH:
		return "ABI mismatch";
	case SYS_ERR_WOULD_BLOCK:
		return "would block";
	case SYS_ERR_CANCELLED:
		return "cancelled";
	case SYS_ERR_OVERFLOW:
		return "overflow";
	case SYS_ERR_IO:
		return "I/O failure";
	case SYS_ERR_PERMISSION:
		return "permission denied";
	default:
		return "unknown error";
	}
}

sys_err_t sys_error_from_errno(int error_number)
{
	switch (error_number) {
	case 0:
		return SYS_OK;
	case EINVAL:
		return SYS_ERR_INVALID_PARAM;
	case ENOMEM:
		return SYS_ERR_NO_MEMORY;
	case ENOENT:
		return SYS_ERR_NOT_FOUND;
	case EEXIST:
		return SYS_ERR_ALREADY_EXISTS;
	case EBUSY:
		return SYS_ERR_BUSY;
	case ETIMEDOUT:
		return SYS_ERR_TIMEOUT;
	case EAGAIN:
		return SYS_ERR_WOULD_BLOCK;
	case ECANCELED:
		return SYS_ERR_CANCELLED;
	case EOVERFLOW:
		return SYS_ERR_OVERFLOW;
	case EACCES:
	case EPERM:
		return SYS_ERR_PERMISSION;
	case EIO:
		return SYS_ERR_IO;
	default:
		return SYS_ERR_GENERIC;
	}
}
