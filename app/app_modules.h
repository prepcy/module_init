#ifndef APP_MODULES_H
#define APP_MODULES_H

#include "sys_core.h"

/**
 * 只有这里是可变的！
 * 团队成员增加新模块，只需要在这里追加枚举。
 * 由于 core 目录不包含此文件，所以修改这里【绝对不会】引发 core 的重新编译！
 */
typedef enum {
	SYS_MOD_WIFI = 0,
	SYS_MOD_GPS,
	SYS_MOD_IMU,
	SYS_MOD_CAMERA,
	SYS_MOD_ZLMEDIA,
	// 未来你可以随意在这里加内容：
	// SYS_MOD_4G,
	// SYS_MOD_BLUETOOTH,
	SYS_MOD_APP_MAX
} sys_mod_id_t;

// 编译期安全防火墙：使用 C11 静态断言校验注册槽位是否溢出
_Static_assert(SYS_MOD_APP_MAX <= SYS_CORE_MAX_SLOTS,
	       "编译错误: app_modules.h 中分配的模块槽位数量超过了框架的最大限制 (SYS_CORE_MAX_SLOTS)!");

#endif // APP_MODULES_H
