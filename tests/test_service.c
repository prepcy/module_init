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

	TEST_CHECK(sys_core_init() == SYS_OK);
	TEST_CHECK(sys_service_register(&service) == SYS_OK);
	TEST_CHECK(sys_service_register(&service) == SYS_ERR_ALREADY_EXISTS);
	TEST_CHECK(sys_service_acquire(TEST_MODULE_ID, TEST_INTERFACE_ID, 2U, sizeof(test_ops_t), &ref) ==
		   SYS_ERR_ABI_MISMATCH);
	TEST_CHECK(sys_service_acquire(TEST_MODULE_ID, TEST_INTERFACE_ID, TEST_ABI_VERSION, sizeof(test_ops_t), &ref) ==
		   SYS_OK);
	const test_ops_t *acquired_ops = ref.ops;
	TEST_CHECK(acquired_ops->add(2, 3) == 5);
	sys_service_release(&ref);
	TEST_CHECK(sys_service_unregister(TEST_MODULE_ID, TEST_INTERFACE_ID) == SYS_OK);
	TEST_CHECK(sys_service_acquire(TEST_MODULE_ID, TEST_INTERFACE_ID, TEST_ABI_VERSION, sizeof(test_ops_t), &ref) ==
		   SYS_ERR_NOT_FOUND);
	sys_core_shutdown();
	return 0;
}
