#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>

#define TEST_CHECK(condition)                                                                                          \
	do {                                                                                                           \
		if (!(condition)) {                                                                                    \
			fprintf(stderr, "test failure: %s (%s:%d)\n", #condition, __FILE__, __LINE__);                 \
			abort();                                                                                       \
		}                                                                                                      \
	} while (0)

#endif
