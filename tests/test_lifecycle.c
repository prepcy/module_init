#include "sys_core.h"
#include "test_common.h"

#define FIRST_COMPONENT_ID 0x9100U
#define SECOND_COMPONENT_ID 0x9200U

static int g_order[8];
static size_t g_order_count;

static sys_err_t first_init(void)
{
	g_order[g_order_count++] = 1;
	return SYS_OK;
}

static void first_exit(void)
{
	g_order[g_order_count++] = -1;
}

static sys_err_t first_start(void)
{
	g_order[g_order_count++] = 11;
	return SYS_OK;
}

static sys_err_t first_stop(void)
{
	g_order[g_order_count++] = -11;
	return SYS_OK;
}

static sys_err_t second_init(void)
{
	g_order[g_order_count++] = 2;
	return SYS_OK;
}

static void second_exit(void)
{
	g_order[g_order_count++] = -2;
}

static sys_err_t second_start(void)
{
	g_order[g_order_count++] = 12;
	return SYS_OK;
}

static sys_err_t second_stop(void)
{
	g_order[g_order_count++] = -12;
	return SYS_OK;
}

static const uint32_t g_second_dependencies[] = {FIRST_COMPONENT_ID};

SYS_COMPONENT_REGISTER(g_first_component, .id = FIRST_COMPONENT_ID, .name = "first",
		       .phase = SYS_COMPONENT_PHASE_SERVICE, .policy = SYS_COMPONENT_REQUIRED, .init = first_init,
		       .start = first_start, .stop = first_stop, .deinit = first_exit);
SYS_COMPONENT_REGISTER(g_second_component, .id = SECOND_COMPONENT_ID, .name = "second",
		       .phase = SYS_COMPONENT_PHASE_SERVICE, .policy = SYS_COMPONENT_REQUIRED,
		       .dependencies = g_second_dependencies, .dependency_count = SYS_ARRAY_SIZE(g_second_dependencies),
		       .init = second_init, .start = second_start, .stop = second_stop, .deinit = second_exit);

int main(void)
{
	TEST_CHECK(sys_core_init() == SYS_OK);
	TEST_CHECK(g_order_count == 4U);
	TEST_CHECK(g_order[0] == 1);
	TEST_CHECK(g_order[1] == 2);
	TEST_CHECK(g_order[2] == 11);
	TEST_CHECK(g_order[3] == 12);
	TEST_CHECK(sys_core_shutdown() == SYS_OK);
	TEST_CHECK(g_order_count == 8U);
	TEST_CHECK(g_order[4] == -12);
	TEST_CHECK(g_order[5] == -11);
	TEST_CHECK(g_order[6] == -2);
	TEST_CHECK(g_order[7] == -1);
	return 0;
}
