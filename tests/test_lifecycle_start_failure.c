#include "sys_core.h"
#include "test_common.h"

#define FIRST_COMPONENT_ID 0x9500U
#define FAILING_COMPONENT_ID 0x9600U

static int g_order[7];
static size_t g_order_count;

static sys_err_t first_init(void)
{
	g_order[g_order_count++] = 1;
	return SYS_OK;
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

static void first_deinit(void)
{
	g_order[g_order_count++] = -1;
}

static sys_err_t failing_init(void)
{
	g_order[g_order_count++] = 2;
	return SYS_OK;
}

static sys_err_t failing_start(void)
{
	g_order[g_order_count++] = 12;
	return SYS_ERR_GENERIC;
}

static void failing_deinit(void)
{
	g_order[g_order_count++] = -2;
}

static const uint32_t g_failing_dependencies[] = {FIRST_COMPONENT_ID};

SYS_COMPONENT_REGISTER(g_first_component, .id = FIRST_COMPONENT_ID, .name = "first",
		       .phase = SYS_COMPONENT_PHASE_SERVICE, .policy = SYS_COMPONENT_REQUIRED, .init = first_init,
		       .start = first_start, .stop = first_stop, .deinit = first_deinit);
SYS_COMPONENT_REGISTER(g_failing_component, .id = FAILING_COMPONENT_ID, .name = "failing",
		       .phase = SYS_COMPONENT_PHASE_SERVICE, .policy = SYS_COMPONENT_REQUIRED,
		       .dependencies = g_failing_dependencies,
		       .dependency_count = SYS_ARRAY_SIZE(g_failing_dependencies), .init = failing_init,
		       .start = failing_start, .deinit = failing_deinit);

static const sys_component_status_t *find_status(const sys_component_status_t *statuses, size_t count, uint32_t id)
{
	for (size_t i = 0; i < count; i++) {
		if (statuses[i].id == id) {
			return &statuses[i];
		}
	}
	return NULL;
}

int main(void)
{
	sys_component_status_t statuses[2];
	const sys_component_status_t *failing_status;

	TEST_CHECK(sys_core_init() == SYS_ERR_GENERIC);
	TEST_CHECK(g_order_count == SYS_ARRAY_SIZE(g_order));
	TEST_CHECK(g_order[0] == 1 && g_order[1] == 2 && g_order[2] == 11 && g_order[3] == 12);
	TEST_CHECK(g_order[4] == -11 && g_order[5] == -2 && g_order[6] == -1);
	TEST_CHECK(sys_component_get_status(statuses, SYS_ARRAY_SIZE(statuses)) == 2U);
	failing_status = find_status(statuses, SYS_ARRAY_SIZE(statuses), FAILING_COMPONENT_ID);
	TEST_CHECK(failing_status != NULL && failing_status->state == SYS_COMPONENT_FAILED);
	TEST_CHECK(failing_status->last_error == SYS_ERR_GENERIC);
	return 0;
}
