#include "nvs_flash.h"
#include "nvs.h"

nvs_handle_t initNvs();
String getNonVolatile(nvs_handle_t &nvs_handle, String par);
void setNonVolatile(nvs_handle_t &nvs_handle,  String par, const char *value);
void setNonVolatile(nvs_handle_t &nvs_handle,  String par, long value);
