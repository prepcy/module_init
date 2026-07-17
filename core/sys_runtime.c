/**
 * @file sys_runtime.c
 * @brief signalfd和eventfd驱动的Linux应用退出运行时。
 */

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "sys_runtime.h"

struct sys_runtime {
	int signal_fd;
	int stop_fd;
	sigset_t previous_mask;
	pthread_t owner;
};

sys_err_t sys_runtime_create(sys_runtime_t **out_runtime)
{
	sys_runtime_t *runtime;
	sigset_t signal_mask;
	int ret;

	if (out_runtime == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	*out_runtime = NULL;
	runtime = calloc(1U, sizeof(*runtime));
	if (runtime == NULL) {
		return SYS_ERR_NO_MEMORY;
	}
	runtime->signal_fd = -1;
	runtime->stop_fd = -1;
	runtime->owner = pthread_self();
	sigemptyset(&signal_mask);
	sigaddset(&signal_mask, SIGINT);
	sigaddset(&signal_mask, SIGTERM);
	ret = pthread_sigmask(SIG_BLOCK, &signal_mask, &runtime->previous_mask);
	if (ret != 0) {
		free(runtime);
		return sys_error_from_errno(ret);
	}
	runtime->signal_fd = signalfd(-1, &signal_mask, SFD_CLOEXEC | SFD_NONBLOCK);
	runtime->stop_fd = eventfd(0U, EFD_CLOEXEC | EFD_NONBLOCK);
	if (runtime->signal_fd < 0 || runtime->stop_fd < 0) {
		int error_number = errno;
		sys_err_t cleanup_ret = sys_runtime_destroy(&runtime);

		return cleanup_ret == SYS_OK ? sys_error_from_errno(error_number) : cleanup_ret;
	}
	*out_runtime = runtime;
	return SYS_OK;
}

sys_err_t sys_runtime_wait(sys_runtime_t *runtime, int timeout_ms)
{
	struct pollfd descriptors[2];
	int ret;

	if (runtime == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	descriptors[0] = (struct pollfd){.fd = runtime->signal_fd, .events = POLLIN};
	descriptors[1] = (struct pollfd){.fd = runtime->stop_fd, .events = POLLIN};
	ret = poll(descriptors, SYS_ARRAY_SIZE(descriptors), timeout_ms);
	if (ret == 0) {
		return SYS_ERR_TIMEOUT;
	}
	if (ret < 0) {
		return errno == EINTR ? SYS_ERR_WOULD_BLOCK : sys_error_from_errno(errno);
	}
	if ((descriptors[0].revents & POLLIN) != 0) {
		struct signalfd_siginfo signal_info;
		if (read(runtime->signal_fd, &signal_info, sizeof(signal_info)) != sizeof(signal_info)) {
			return sys_error_from_errno(errno);
		}
		return SYS_ERR_CANCELLED;
	}
	if ((descriptors[1].revents & POLLIN) != 0) {
		uint64_t value;
		if (read(runtime->stop_fd, &value, sizeof(value)) != sizeof(value)) {
			return sys_error_from_errno(errno);
		}
		return SYS_ERR_CANCELLED;
	}
	if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
	    (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
		return SYS_ERR_IO;
	}
	return SYS_ERR_IO;
}

sys_err_t sys_runtime_request_stop(sys_runtime_t *runtime)
{
	uint64_t value = 1U;

	if (runtime == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	return write(runtime->stop_fd, &value, sizeof(value)) == sizeof(value) ? SYS_OK : sys_error_from_errno(errno);
}

sys_err_t sys_runtime_destroy(sys_runtime_t **runtime_ptr)
{
	sys_runtime_t *runtime;
	sys_err_t ret = SYS_OK;
	int mask_ret;

	if (runtime_ptr == NULL || *runtime_ptr == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	runtime = *runtime_ptr;
	if (!pthread_equal(runtime->owner, pthread_self())) {
		return SYS_ERR_STATE;
	}
	if (runtime->signal_fd >= 0) {
		if (close(runtime->signal_fd) != 0) {
			ret = sys_error_from_errno(errno);
		}
	}
	if (runtime->stop_fd >= 0) {
		if (close(runtime->stop_fd) != 0 && ret == SYS_OK) {
			ret = sys_error_from_errno(errno);
		}
	}
	mask_ret = pthread_sigmask(SIG_SETMASK, &runtime->previous_mask, NULL);
	free(runtime);
	*runtime_ptr = NULL;
	if (mask_ret != 0 && ret == SYS_OK) {
		ret = sys_error_from_errno(mask_ret);
	}
	return ret;
}
