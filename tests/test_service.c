#include "sys_core.h"
#include "test_common.h"

#define TEST_MODULE_ID 0x9000U
#define TEST_INTERFACE_ID 1U
#define TEST_ABI_VERSION 1U

typedef struct {
	int (*add)(int left, int right);
} test_ops_t;

static int add_values(int left, int right)
{
	return left + right;
}

int main(void)
{
	static const test_ops_t ops = {.add = add_values};
	const sys_service_desc_t service = {.module_id = TEST_MODULE_ID,
					    .interface_id = TEST_INTERFACE_ID,
					    .abi_version = TEST_ABI_VERSION,
					    .ops_size = sizeof(ops),
					    .ops = &ops,
					    .name = "test.control"};
	sys_service_ref_t ref;
	sys_service_status_t status;

	TEST_CHECK(sys_core_init() == SYS_OK);
	ref.ops = &ops;
	ref.internal = (void *)(uintptr_t)1U;
	TEST_CHECK(sys_service_acquire(0U, TEST_INTERFACE_ID, TEST_ABI_VERSION, sizeof(test_ops_t), &ref) ==
		   SYS_ERR_INVALID_PARAM);
	TEST_CHECK(ref.ops == NULL && ref.internal == NULL);
	TEST_CHECK(sys_service_register(&service) == SYS_OK);
	TEST_CHECK(sys_service_get_status(&status, 1U) == 1U);
	TEST_CHECK(status.module_id == TEST_MODULE_ID && status.interface_id == TEST_INTERFACE_ID);
	TEST_CHECK(status.abi_version == TEST_ABI_VERSION && status.reference_count == 0U && status.accepting);
	TEST_CHECK(sys_service_register(&service) == SYS_ERR_ALREADY_EXISTS);
	TEST_CHECK(sys_service_acquire(TEST_MODULE_ID, TEST_INTERFACE_ID, 2U, sizeof(test_ops_t), &ref) ==
		   SYS_ERR_ABI_MISMATCH);
	TEST_CHECK(sys_service_acquire(TEST_MODULE_ID, TEST_INTERFACE_ID, TEST_ABI_VERSION, sizeof(test_ops_t), &ref) ==
		   SYS_OK);
	TEST_CHECK(sys_service_get_status(&status, 1U) == 1U && status.reference_count == 1U);
	const test_ops_t *acquired_ops = ref.ops;
	TEST_CHECK(acquired_ops->add(2, 3) == 5);
	sys_service_release(&ref);
	TEST_CHECK(sys_service_unregister(TEST_MODULE_ID, TEST_INTERFACE_ID) == SYS_OK);
	TEST_CHECK(sys_service_get_status(NULL, 0U) == 0U);
	TEST_CHECK(sys_service_acquire(TEST_MODULE_ID, TEST_INTERFACE_ID, TEST_ABI_VERSION, sizeof(test_ops_t), &ref) ==
		   SYS_ERR_NOT_FOUND);
	TEST_CHECK(sys_core_shutdown() == SYS_OK);
	return 0;
}
