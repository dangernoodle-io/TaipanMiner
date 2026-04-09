#pragma once

#include <stdbool.h>

// Stratum v1 client task — runs on Core 0, priority 5
void stratum_task(void *arg);

// Get stratum connection status
bool stratum_is_connected(void);
