#include "preferences.h"

uint32_t _handle = 0;
bool _started = false;
bool _readOnly = false;

static const char *TAG = "PREFERENCES";

const char *nvs_errors[] = {"OTHER", "NOT_INITIALIZED", "NOT_FOUND", "TYPE_MISMATCH", "READ_ONLY", "NOT_ENOUGH_SPACE", "INVALID_NAME", "INVALID_HANDLE", "REMOVE_FAILED", "KEY_TOO_LONG", "PAGE_FULL", "INVALID_STATE", "INVALID_LENGTH"};
#define nvs_error(e) (((e) > ESP_ERR_NVS_BASE) ? nvs_errors[(e) & ~(ESP_ERR_NVS_BASE)] : nvs_errors[0])

bool pref_begin(const char *name, bool readOnly, const char *partition_label)
{
    if (_started)
    {
        return false;
    }
    _readOnly = readOnly;
    esp_err_t err = ESP_OK;
    if (partition_label != NULL)
    {
        err = nvs_flash_init_partition(partition_label);
        if (err)
        {
            //log_e("nvs_flash_init_partition failed: %s", nvs_error(err));
            ESP_LOGI(TAG, "nvs_flash_init_partition failed: %s", nvs_error(err));
            return false;
        }
        err = nvs_open_from_partition(partition_label, name, readOnly ? NVS_READONLY : NVS_READWRITE, &_handle);
    }
    else
    {
        err = nvs_open(name, readOnly ? NVS_READONLY : NVS_READWRITE, &_handle);
    }
    if (err)
    {
        //log_e("nvs_open failed: %s", nvs_error(err));
        ESP_LOGI(TAG, "nvs_open failed: %s", nvs_error(err));
        return false;
    }
    _started = true;
    return true;
}

size_t pref_putUInt(const char *key, uint32_t value)
{
    if (!_started || !key || _readOnly)
    {
        return 0;
    }
    esp_err_t err = nvs_set_u32(_handle, key, value);
    if (err)
    {
        //log_e("nvs_set_u32 fail: %s %s", key, nvs_error(err));
        ESP_LOGI(TAG, "nvs_set_u32 fail: %s %s", key, nvs_error(err));
        return 0;
    }
    err = nvs_commit(_handle);
    if (err)
    {
        //log_e("nvs_commit fail: %s %s", key, nvs_error(err));
        ESP_LOGI(TAG, "nvs_commit fail: %s %s", key, nvs_error(err));
        return 0;
    }
    return 4;
}

uint32_t pref_getUInt(const char *key, const uint32_t defaultValue)
{
    uint32_t value = defaultValue;
    if (!_started || !key)
    {
        return value;
    }
    esp_err_t err = nvs_get_u32(_handle, key, &value);
    if (err)
    {
        //log_v("nvs_get_u32 fail: %s %s", key, nvs_error(err));
        ESP_LOGI(TAG, "nvs_get_u32 fail: %s %s", key, nvs_error(err));
    }
    return value;
}