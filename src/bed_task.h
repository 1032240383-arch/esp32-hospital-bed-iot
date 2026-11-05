#pragma once

#include "shared_types.h"

void bed_task_init(void);
bool bed_set_angle(int angle);
bool bed_set_preset(BedPreset_t preset);
