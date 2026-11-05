#pragma once

#include "shared_types.h"

void medication_task_init(void);
bool medication_request_rate(float rate, char* outReason, size_t reasonSize);
bool medication_confirm(char* outMsg, size_t msgSize);
