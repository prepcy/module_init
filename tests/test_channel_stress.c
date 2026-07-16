#include <pthread.h>
#include <sched.h>
#include <stdint.h>

#include "sys_channel.h"
#include "test_common.h"

#define STRESS_FRAME_COUNT 10000U

typedef struct {
	sys_buffer_pool_t *pool;
	sys_channel_t *channel;
	uint64_t consumed_count;
} stress_context_t;

static void *produce_frames(void *argument)
{
	stress_context_t *context = argument;

	for (uint64_t sequence = 0U; sequence < STRESS_FRAME_COUNT; sequence++) {
		sys_buffer_t *buffer = NULL;

		while (sys_buffer_acquire(context->pool, &buffer) == SYS_ERR_BUSY) {
			sched_yield();
		}
		TEST_CHECK(buffer != NULL);
		sys_buffer_set_sequence(buffer, sequence);
		TEST_CHECK(sys_channel_send(context->channel, buffer, -1) == SYS_OK);
		sys_buffer_release(buffer);
	}
	sys_channel_close(context->channel);
	return NULL;
}

static void *consume_frames(void *argument)
{
	stress_context_t *context = argument;
	uint64_t expected_sequence = 0U;

	for (;;) {
		sys_buffer_t *buffer = NULL;
		sys_err_t ret = sys_channel_receive(context->channel, &buffer, -1);

		if (ret == SYS_ERR_CLOSED) {
			break;
		}
		TEST_CHECK(ret == SYS_OK);
		TEST_CHECK(sys_buffer_sequence(buffer) == expected_sequence);
		expected_sequence++;
		sys_buffer_release(buffer);
	}
	context->consumed_count = expected_sequence;
	return NULL;
}

int main(void)
{
	stress_context_t context = {0};
	pthread_t producer;
	pthread_t consumer;

	TEST_CHECK(sys_buffer_pool_create(64U, 256U, &context.pool) == SYS_OK);
	TEST_CHECK(sys_channel_create(32U, &context.channel) == SYS_OK);
	TEST_CHECK(pthread_create(&producer, NULL, produce_frames, &context) == 0);
	TEST_CHECK(pthread_create(&consumer, NULL, consume_frames, &context) == 0);
	TEST_CHECK(pthread_join(producer, NULL) == 0);
	TEST_CHECK(pthread_join(consumer, NULL) == 0);
	TEST_CHECK(context.consumed_count == STRESS_FRAME_COUNT);
	sys_channel_release(context.channel);
	TEST_CHECK(sys_buffer_pool_wait_idle(context.pool, 0) == SYS_OK);
	TEST_CHECK(sys_buffer_pool_destroy(&context.pool) == SYS_OK);
	return 0;
}
