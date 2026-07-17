#include <string.h>

#include "sys_channel.h"
#include "test_common.h"

static void test_fail_when_full(void)
{
	sys_buffer_pool_t *pool = NULL;
	sys_channel_t *channel = NULL;
	sys_buffer_t *first = NULL;
	sys_buffer_t *second = NULL;
	sys_buffer_t *received = NULL;
	sys_channel_stats_t stats;
	sys_buffer_pool_t *invalid_pool = (sys_buffer_pool_t *)(uintptr_t)1U;
	sys_channel_t *invalid_channel = (sys_channel_t *)(uintptr_t)1U;

	TEST_CHECK(sys_buffer_pool_create(0U, 128U, &invalid_pool) == SYS_ERR_INVALID_PARAM);
	TEST_CHECK(invalid_pool == NULL);
	TEST_CHECK(sys_channel_create(0U, &invalid_channel) == SYS_ERR_INVALID_PARAM);
	TEST_CHECK(invalid_channel == NULL);
	TEST_CHECK(sys_buffer_pool_create(2U, 128U, &pool) == SYS_OK);
	TEST_CHECK(sys_channel_create(1U, &channel) == SYS_OK);
	TEST_CHECK(sys_buffer_acquire(pool, &first) == SYS_OK);
	TEST_CHECK(sys_buffer_acquire(pool, &second) == SYS_OK);
	memcpy(sys_buffer_data(first), "frame", sizeof("frame"));
	TEST_CHECK(sys_buffer_set_size(first, sizeof("frame")) == SYS_OK);
	sys_buffer_set_contract(first, 7U, 2U, 11U);
	TEST_CHECK(sys_channel_send(channel, first, 0) == SYS_OK);
	TEST_CHECK(sys_channel_send(channel, second, 0) == SYS_ERR_QUEUE_FULL);
	TEST_CHECK(sys_channel_get_stats(channel, &stats) == SYS_OK);
	TEST_CHECK(stats.sent_count == 1U && stats.full_count == 1U && stats.high_watermark == 1U);
	sys_buffer_release(first);
	sys_buffer_release(second);

	TEST_CHECK(sys_channel_receive(channel, &received, 0) == SYS_OK);
	TEST_CHECK(sys_buffer_size(received) == sizeof("frame"));
	TEST_CHECK(strcmp(sys_buffer_data(received), "frame") == 0);
	TEST_CHECK(sys_buffer_type(received) == 7U);
	TEST_CHECK(sys_buffer_version(received) == 2U);
	TEST_CHECK(sys_buffer_generation(received) == 11U);
	sys_buffer_release(received);
	TEST_CHECK(sys_channel_get_stats(channel, &stats) == SYS_OK);
	TEST_CHECK(stats.received_count == 1U && stats.current_depth == 0U && stats.capacity == 1U);
	sys_channel_close(channel);
	TEST_CHECK(sys_channel_receive(channel, &received, 0) == SYS_ERR_CLOSED);
	sys_channel_release(channel);
	TEST_CHECK(sys_buffer_pool_wait_idle(pool, 0) == SYS_OK);
	TEST_CHECK(sys_buffer_pool_destroy(&pool) == SYS_OK);
}

static void test_drop_oldest_when_full(void)
{
	sys_buffer_pool_t *pool = NULL;
	sys_channel_t *channel = NULL;
	sys_buffer_t *buffers[3] = {NULL};
	sys_channel_stats_t stats;
	const sys_channel_config_t config = {.capacity = 2U, .full_policy = SYS_CHANNEL_FULL_DROP_OLDEST};

	TEST_CHECK(sys_buffer_pool_create(3U, 64U, &pool) == SYS_OK);
	TEST_CHECK(sys_channel_create_with_config(&config, &channel) == SYS_OK);
	for (size_t i = 0; i < SYS_ARRAY_SIZE(buffers); i++) {
		TEST_CHECK(sys_buffer_acquire(pool, &buffers[i]) == SYS_OK);
		sys_buffer_set_sequence(buffers[i], i + 1U);
		TEST_CHECK(sys_channel_send(channel, buffers[i], 0) == SYS_OK);
		sys_buffer_release(buffers[i]);
	}
	for (uint64_t expected = 2U; expected <= 3U; expected++) {
		sys_buffer_t *received = NULL;

		TEST_CHECK(sys_channel_receive(channel, &received, 0) == SYS_OK);
		TEST_CHECK(sys_buffer_sequence(received) == expected);
		sys_buffer_release(received);
	}
	TEST_CHECK(sys_channel_get_stats(channel, &stats) == SYS_OK);
	TEST_CHECK(stats.sent_count == 3U && stats.received_count == 2U);
	TEST_CHECK(stats.dropped_count == 1U && stats.full_count == 1U && stats.high_watermark == 2U);
	sys_channel_release(channel);
	TEST_CHECK(sys_buffer_pool_destroy(&pool) == SYS_OK);
}

int main(void)
{
	test_fail_when_full();
	test_drop_oldest_when_full();
	return 0;
}
