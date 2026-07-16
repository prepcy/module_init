#include <string.h>

#include "sys_channel.h"
#include "test_common.h"

int main(void)
{
	sys_buffer_pool_t *pool = NULL;
	sys_channel_t *channel = NULL;
	sys_buffer_t *first = NULL;
	sys_buffer_t *second = NULL;
	sys_buffer_t *received = NULL;

	TEST_CHECK(sys_buffer_pool_create(2U, 128U, &pool) == SYS_OK);
	TEST_CHECK(sys_channel_create(1U, &channel) == SYS_OK);
	TEST_CHECK(sys_buffer_acquire(pool, &first) == SYS_OK);
	TEST_CHECK(sys_buffer_acquire(pool, &second) == SYS_OK);
	memcpy(sys_buffer_data(first), "frame", sizeof("frame"));
	TEST_CHECK(sys_buffer_set_size(first, sizeof("frame")) == SYS_OK);
	TEST_CHECK(sys_channel_send(channel, first, 0) == SYS_OK);
	TEST_CHECK(sys_channel_send(channel, second, 0) == SYS_ERR_QUEUE_FULL);
	sys_buffer_release(first);
	sys_buffer_release(second);

	TEST_CHECK(sys_channel_receive(channel, &received, 0) == SYS_OK);
	TEST_CHECK(sys_buffer_size(received) == sizeof("frame"));
	TEST_CHECK(strcmp(sys_buffer_data(received), "frame") == 0);
	sys_buffer_release(received);
	sys_channel_close(channel);
	TEST_CHECK(sys_channel_receive(channel, &received, 0) == SYS_ERR_CLOSED);
	sys_channel_release(channel);
	TEST_CHECK(sys_buffer_pool_wait_idle(pool, 0) == SYS_OK);
	TEST_CHECK(sys_buffer_pool_destroy(&pool) == SYS_OK);
	return 0;
}
