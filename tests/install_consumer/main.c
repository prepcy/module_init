#include "sys_core.h"

int main(void)
{
	if (APP_CORE_VERSION_MAJOR != 3U || sys_core_init() != SYS_OK) {
		return 1;
	}
	return sys_core_shutdown() == SYS_OK ? 0 : 1;
}
