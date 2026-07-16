#include "sys_core.h"
#include "test_common.h"

#define READY_COMPONENT_ID 0x9300U
#define FAILING_COMPONENT_ID 0x9400U

static int g_ready_init_count;
static int g_ready_exit_count;

static sys_err_t ready_init(void)
{
	g_ready_init_count++;
	return SYS_OK;
}

static void ready_exit(void)
{
	g_ready_exit_count++;
}

static sys_err_t failing_init(void)
{
	return SYS_ERR_GENERIC;
}

static const uint32_t g_failing_dependencies[] = {READY_COMPONENT_ID};

SYS_COMPONENT_REGISTER(g_ready_component, READY_COMPONENT_ID, "ready", SYS_COMPONENT_PHASE_SERVICE, NULL, 0U,
		       ready_init, ready_exit);
SYS_COMPONENT_REGISTER(g_failing_component, FAILING_COMPONENT_ID, "failing", SYS_COMPONENT_PHASE_SERVICE,
		       g_failing_dependencies, SYS_ARRAY_SIZE(g_failing_dependencies), failing_init, NULL);

int main(void)
{
	TEST_CHECK(sys_core_init() == SYS_ERR_GENERIC);
	TEST_CHECK(g_ready_init_count == 1);
	TEST_CHECK(g_ready_exit_count == 1);
	TEST_CHECK(!sys_core_is_running());
	return 0;
}
