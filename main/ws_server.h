// ws_server.h
#pragma once

#include <stdbool.h>

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

void start_websocket_server(httpd_handle_t server);

// Return true when a client is actively connected/streaming
bool ws_is_streaming(void);

#ifdef __cplusplus
}
#endif
