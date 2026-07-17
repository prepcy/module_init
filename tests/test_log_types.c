#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "sys_core.h"
#include "test_common.h"

typedef struct {
	size_t count;
	sys_log_level_t level;
	char message[64];
} log_capture_t;

static void capture_log(const sys_log_record_t *record, void *private_data)
{
	log_capture_t *capture = private_data;

	capture->count++;
	capture->level = record->level;
	(void)snprintf(capture->message, sizeof(capture->message), "%s", record->message);
}

static void test_log_filter_and_sink(void)
{
	log_capture_t capture = {0};

	sys_log_set_sink(capture_log, &capture);
	sys_log_set_level(SYS_LOG_WARNING);
	SYS_LOG_INFO_MSG("test", "hidden");
	TEST_CHECK(capture.count == 0U);
	SYS_LOG_ERROR_MSG("test", "failure %d", 7);
	TEST_CHECK(capture.count == 1U && capture.level == SYS_LOG_ERROR);
	TEST_CHECK(strcmp(capture.message, "failure 7") == 0);
	sys_log_set_sink(NULL, NULL);
	sys_log_set_level(SYS_LOG_INFO);
}

static void test_error_mapping(void)
{
	TEST_CHECK(strcmp(sys_error_string(SYS_ERR_TIMEOUT), "timeout") == 0);
	TEST_CHECK(strcmp(sys_error_string((sys_err_t)-999), "unknown error") == 0);
	TEST_CHECK(sys_error_from_errno(0) == SYS_OK);
	TEST_CHECK(sys_error_from_errno(EINVAL) == SYS_ERR_INVALID_PARAM);
	TEST_CHECK(sys_error_from_errno(EACCES) == SYS_ERR_PERMISSION);
	TEST_CHECK(sys_error_from_errno(EIO) == SYS_ERR_IO);
}

int main(void)
{
	test_log_filter_and_sink();
	test_error_mapping();
	return 0;
}
