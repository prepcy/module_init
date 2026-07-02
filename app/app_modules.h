#ifndef APP_MODULES_H
#define APP_MODULES_H

/**
 * 🌟 只有这里是可变的！
 * 团队成员增加新模块，只需要在这里追加枚举。
 * 由于 core 目录不包含此文件，所以修改这里【绝对不会】引发 core 的重新编译！
 */
typedef enum {
	SYS_MOD_WIFI = 0,
	SYS_MOD_CAMERA,
	SYS_MOD_IMU,
	// 未来你可以随意在这里加内容：
	// SYS_MOD_4G,
	// SYS_MOD_BLUETOOTH,
	SYS_MOD_APP_MAX
} sys_mod_id_t;

#endif // APP_MODULES_H
