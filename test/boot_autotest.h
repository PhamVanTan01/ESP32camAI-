#pragma once

#include "esp_err.h"

typedef esp_err_t (*boot_autotest_action_fn_t)(void);

void boot_autotest_start(boot_autotest_action_fn_t photo_action,
                         boot_autotest_action_fn_t record_action);
