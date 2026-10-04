#pragma once
#include <ctime>

// SDK clock domain, separate from the build host's clockid_t and clock IDs.
int nocturne_ps4_clock_gettime(int clock_id, struct timespec* output);
