#include <Wstring.h>
#include "storage.h"

nvs_handle_t initNvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  nvs_handle_t nvs_handle;
  err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  ESP_ERROR_CHECK(err);
  return nvs_handle;
}

String getNonVolatile(nvs_handle_t &nvs_handle, String par) {
   size_t required_size = 0;
   esp_err_t err = nvs_get_str(nvs_handle, par.c_str(), NULL, &required_size);
   if (err == ESP_OK) {
    char * value = (char *) malloc(required_size);
    err = nvs_get_str(nvs_handle, par.c_str(), value, &required_size);
    if (err == ESP_OK) {
       return value;
    }
    free(value);
   }
   return "";
}

void setNonVolatile(nvs_handle_t &nvs_handle, String par, const char *value) {
  ESP_ERROR_CHECK(nvs_set_str(nvs_handle, par.c_str(), value));
}

void setNonVolatile(nvs_handle_t &nvs_handle, String par, long value) {
  char buf[50];
  sprintf(buf, "%ld", value);
  setNonVolatile(nvs_handle, par, buf);
}
