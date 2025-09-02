// ws_server.h
#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

void start_websocket_server(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
