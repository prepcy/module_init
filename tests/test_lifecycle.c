#include "sys_core.h"
#include "test_common.h"

#define FIRST_COMPONENT_ID 0x9100U
#define SECOND_COMPONENT_ID 0x9200U

static int g_order[4];
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

static sys_err_t second_init(void)
{
	g_order[g_order_count++] = 2;
	return SYS_OK;
}

static void second_exit(void)
{
	g_order[g_order_count++] = -2;
}

static const uint32_t g_second_dependencies[] = {FIRST_COMPONENT_ID};

SYS_COMPONENT_REGISTER(g_first_component, FIRST_COMPONENT_ID, "first", SYS_COMPONENT_PHASE_SERVICE, NULL, 0U,
		       first_init, first_exit);
SYS_COMPONENT_REGISTER(g_second_component, SECOND_COMPONENT_ID, "second", SYS_COMPONENT_PHASE_SERVICE,
		       g_second_dependencies, SYS_ARRAY_SIZE(g_second_dependencies), second_init, second_exit);

int main(void)
{
	TEST_CHECK(sys_core_init() == SYS_OK);
	TEST_CHECK(g_order_count == 2U);
	TEST_CHECK(g_order[0] == 1);
	TEST_CHECK(g_order[1] == 2);
	sys_core_shutdown();
	TEST_CHECK(g_order_count == 4U);
	TEST_CHECK(g_order[2] == -2);
	TEST_CHECK(g_order[3] == -1);
	return 0;
}
