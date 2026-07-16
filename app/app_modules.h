/**
 * @file app_modules.h
 * @brief 应用模块稳定标识定义。
 */

#ifndef APP_MODULES_H
#define APP_MODULES_H

#include <stdint.h>

/* 模块 ID 是稳定接口的一部分，禁止通过插入枚举项改变已有值。 */
enum {
	SYS_MOD_WIFI = 0x0100U,
	SYS_MOD_GPS = 0x0200U,
	SYS_MOD_IMU = 0x0300U,
	SYS_MOD_CAMERA = 0x0400U,
	SYS_MOD_ZLMEDIA = 0x0500U
};

#endif
