#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_http_server.h"

// Start the HTTP server and return its handle.
// Returns NULL if the server fails to start.
httpd_handle_t start_http_server(void);

#endif // HTTP_SERVER_H
