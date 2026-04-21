#include "app_cli_auto_test.h"

#include "esp_log.h"
#include "app_cli.h"

static const char *TAG = "app_cli_test";

void app_cli_auto_test_run(void)
{
    esp_err_t start_before_init = app_cli_start();
    if (start_before_init == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "APP CLI AUTO TEST PASS");
    } else {
        ESP_LOGE(TAG, "APP CLI AUTO TEST FAIL: start_before_init=%s",
                 esp_err_to_name(start_before_init));
    }
}
