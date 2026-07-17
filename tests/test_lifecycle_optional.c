#include "sys_core.h"
#include "test_common.h"

#define OPTIONAL_COMPONENT_ID 0x9700U
#define REQUIRED_COMPONENT_ID 0x9800U

static sys_err_t optional_init(void)
{
	return SYS_ERR_GENERIC;
}

static sys_err_t required_init(void)
{
	return SYS_OK;
}

SYS_COMPONENT_REGISTER(g_optional_component, .id = OPTIONAL_COMPONENT_ID, .name = "optional",
		       .phase = SYS_COMPONENT_PHASE_SERVICE, .policy = SYS_COMPONENT_OPTIONAL, .init = optional_init);
SYS_COMPONENT_REGISTER(g_required_component, .id = REQUIRED_COMPONENT_ID, .name = "required",
		       .phase = SYS_COMPONENT_PHASE_SERVICE, .policy = SYS_COMPONENT_REQUIRED, .init = required_init);

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
	const sys_component_status_t *optional_status;
	const sys_component_status_t *required_status;
	sys_core_status_t core_status;

	TEST_CHECK(sys_core_init() == SYS_OK);
	TEST_CHECK(sys_component_get_status(statuses, SYS_ARRAY_SIZE(statuses)) == 2U);
	optional_status = find_status(statuses, SYS_ARRAY_SIZE(statuses), OPTIONAL_COMPONENT_ID);
	required_status = find_status(statuses, SYS_ARRAY_SIZE(statuses), REQUIRED_COMPONENT_ID);
	TEST_CHECK(optional_status != NULL && optional_status->state == SYS_COMPONENT_FAILED);
	TEST_CHECK(required_status != NULL && required_status->state == SYS_COMPONENT_RUNNING);
	sys_core_get_status(&core_status);
	TEST_CHECK(core_status.running && core_status.component_count == 2U);
	TEST_CHECK(core_status.running_component_count == 1U && core_status.failed_component_count == 1U);
	TEST_CHECK(core_status.active_thread_count == 1U && core_status.unjoined_thread_count == 1U);
	TEST_CHECK(sys_core_shutdown() == SYS_OK);
	TEST_CHECK(sys_thread_active_count() == 0U && sys_thread_unjoined_count() == 0U);
	return 0;
}
