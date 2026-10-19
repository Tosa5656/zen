#ifndef TIME_H
#define TIME_H

#include <stdint.h>

typedef int64_t time_t;

struct timespec
{
	time_t tv_sec;
	long tv_nsec;
};

#endif
