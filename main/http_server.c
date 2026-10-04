#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_http_server.h"

#include "singlecan_can.h"
#include "singlecan_commands.h"
#include "tcp_server.h"   // for g_tcp_client_sock

static const char *TAG_HTTP = "HTTP_SERVER";

// We rely on these symbols from other modules:
// - g_can_enabled (in singlecan_can.c)
// - g_tcp_client_sock (in tcp_server.c)
extern bool g_can_enabled;
extern int  g_tcp_client_sock;

// ------------------------------------------------------------
// ROOT HANDLER — JSON STATUS
// ------------------------------------------------------------
static esp_err_t http_root_handler(httpd_req_t *req)
{
    char json[256];

    snprintf(json, sizeof(json),
        "{"
        "\"device\":\"SingleCAN-WROOM32\","
        "\"wifi\":\"connected\","
        "\"can_enabled\":%s,"
        "\"tcp_client_connected\":%s"
        "}",
        g_can_enabled ? "true" : "false",
        (g_tcp_client_sock >= 0) ? "true" : "false"
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

// ------------------------------------------------------------
// HTTP HANDLERS
// ------------------------------------------------------------

// /ping
static esp_err_t http_ping_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "PONG\n");
    return ESP_OK;
}

// /status
static esp_err_t http_status_handler(httpd_req_t *req)
{
    char buf[256];
    singlecan_get_status(buf, sizeof(buf));
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// /enable_can
static esp_err_t http_enable_can_handler(httpd_req_t *req)
{
    singlecan_enable_can();
    httpd_resp_sendstr(req, "CAN ENABLED\n");
    return ESP_OK;
}

// /disable_can
static esp_err_t http_disable_can_handler(httpd_req_t *req)
{
    singlecan_disable_can();
    httpd_resp_sendstr(req, "CAN DISABLED\n");
    return ESP_OK;
}

// Doors
static esp_err_t http_lock_handler(httpd_req_t *req)
{
    singlecan_cmd_lock_doors();
    httpd_resp_sendstr(req, "LOCK_DOORS\n");
    return ESP_OK;
}

static esp_err_t http_unlock_handler(httpd_req_t *req)
{
    singlecan_cmd_unlock_doors();
    httpd_resp_sendstr(req, "UNLOCK_DOORS\n");
    return ESP_OK;
}

// Windows
static esp_err_t http_windows_down_handler(httpd_req_t *req)
{
    singlecan_cmd_windows_down();
    httpd_resp_sendstr(req, "WINDOWS_DOWN\n");
    return ESP_OK;
}

static esp_err_t http_windows_up_handler(httpd_req_t *req)
{
    singlecan_cmd_windows_up();
    httpd_resp_sendstr(req, "WINDOWS_UP\n");
    return ESP_OK;
}

// Sunroof
static esp_err_t http_sunroof_open_handler(httpd_req_t *req)
{
    singlecan_cmd_sunroof_open();
    httpd_resp_sendstr(req, "SUNROOF_OPEN\n");
    return ESP_OK;
}

static esp_err_t http_sunroof_close_handler(httpd_req_t *req)
{
    singlecan_cmd_sunroof_close();
    httpd_resp_sendstr(req, "SUNROOF_CLOSE\n");
    return ESP_OK;
}

static esp_err_t http_sunroof_vent_handler(httpd_req_t *req)
{
    singlecan_cmd_sunroof_vent();
    httpd_resp_sendstr(req, "SUNROOF_VENT\n");
    return ESP_OK;
}

// Lights
static esp_err_t http_headlights_on_handler(httpd_req_t *req)
{
    singlecan_cmd_headlights_on();
    httpd_resp_sendstr(req, "HEADLIGHTS_ON\n");
    return ESP_OK;
}

static esp_err_t http_headlights_off_handler(httpd_req_t *req)
{
    singlecan_cmd_headlights_off();
    httpd_resp_sendstr(req, "HEADLIGHTS_OFF\n");
    return ESP_OK;
}

static esp_err_t http_fog_on_handler(httpd_req_t *req)
{
    singlecan_cmd_fog_lights_on();
    httpd_resp_sendstr(req, "FOG_LIGHTS_ON\n");
    return ESP_OK;
}

static esp_err_t http_fog_off_handler(httpd_req_t *req)
{
    singlecan_cmd_fog_lights_off();
    httpd_resp_sendstr(req, "FOG_LIGHTS_OFF\n");
    return ESP_OK;
}

static esp_err_t http_interior_on_handler(httpd_req_t *req)
{
    singlecan_cmd_interior_lights_on();
    httpd_resp_sendstr(req, "INTERIOR_LIGHTS_ON\n");
    return ESP_OK;
}

static esp_err_t http_interior_off_handler(httpd_req_t *req)
{
    singlecan_cmd_interior_lights_off();
    httpd_resp_sendstr(req, "INTERIOR_LIGHTS_OFF\n");
    return ESP_OK;
}

// Climate
static esp_err_t http_ac_on_handler(httpd_req_t *req)
{
    singlecan_cmd_ac_on();
    httpd_resp_sendstr(req, "AC_ON\n");
    return ESP_OK;
}

static esp_err_t http_ac_off_handler(httpd_req_t *req)
{
    singlecan_cmd_ac_off();
    httpd_resp_sendstr(req, "AC_OFF\n");
    return ESP_OK;
}

static esp_err_t http_fan_up_handler(httpd_req_t *req)
{
    singlecan_cmd_fan_up();
    httpd_resp_sendstr(req, "FAN_UP\n");
    return ESP_OK;
}

static esp_err_t http_fan_down_handler(httpd_req_t *req)
{
    singlecan_cmd_fan_down();
    httpd_resp_sendstr(req, "FAN_DOWN\n");
    return ESP_OK;
}

// Audio
static esp_err_t http_audio_mute_handler(httpd_req_t *req)
{
    singlecan_cmd_audio_mute();
    httpd_resp_sendstr(req, "AUDIO_MUTE\n");
    return ESP_OK;
}

static esp_err_t http_audio_unmute_handler(httpd_req_t *req)
{
    singlecan_cmd_audio_unmute();
    httpd_resp_sendstr(req, "AUDIO_UNMUTE\n");
    return ESP_OK;
}

// Trunk
static esp_err_t http_trunk_open_handler(httpd_req_t *req)
{
    singlecan_cmd_trunk_open();
    httpd_resp_sendstr(req, "TRUNK_OPEN\n");
    return ESP_OK;
}

// Horn
static esp_err_t http_horn_handler(httpd_req_t *req)
{
    singlecan_cmd_horn();
    httpd_resp_sendstr(req, "HORN\n");
    return ESP_OK;
}

// Hazards
static esp_err_t http_hazards_on_handler(httpd_req_t *req)
{
    singlecan_cmd_hazards_on();
    httpd_resp_sendstr(req, "HAZARDS_ON\n");
    return ESP_OK;
}

static esp_err_t http_hazards_off_handler(httpd_req_t *req)
{
    singlecan_cmd_hazards_off();
    httpd_resp_sendstr(req, "HAZARDS_OFF\n");
    return ESP_OK;
}

// Defrost
static esp_err_t http_defrost_on_handler(httpd_req_t *req)
{
    singlecan_cmd_defrost_on();
    httpd_resp_sendstr(req, "DEFROST_ON\n");
    return ESP_OK;
}

static esp_err_t http_defrost_off_handler(httpd_req_t *req)
{
    singlecan_cmd_defrost_off();
    httpd_resp_sendstr(req, "DEFROST_OFF\n");
    return ESP_OK;
}

// ------------------------------------------------------------
// START HTTP SERVER — FIXED HANDLER LIMIT + ROOT HANDLER
// ------------------------------------------------------------
httpd_handle_t start_http_server(void)
{
    httpd_handle_t server = NULL;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.ctrl_port        = 32768;
    config.max_uri_handlers = 32;   // FIXED

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Failed to start HTTP server");
        return NULL;
    }

    // Root JSON status
    httpd_register_uri_handler(server, &(httpd_uri_t){"/", HTTP_GET, http_root_handler, NULL});

    // Core endpoints
    httpd_register_uri_handler(server, &(httpd_uri_t){"/ping", HTTP_GET, http_ping_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/status", HTTP_GET, http_status_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/enable_can", HTTP_GET, http_enable_can_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/disable_can", HTTP_GET, http_disable_can_handler, NULL});

    // Doors
    httpd_register_uri_handler(server, &(httpd_uri_t){"/lock", HTTP_GET, http_lock_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/unlock", HTTP_GET, http_unlock_handler, NULL});

    // Windows
    httpd_register_uri_handler(server, &(httpd_uri_t){"/windows_down", HTTP_GET, http_windows_down_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/windows_up", HTTP_GET, http_windows_up_handler, NULL});

    // Sunroof
    httpd_register_uri_handler(server, &(httpd_uri_t){"/sunroof_open", HTTP_GET, http_sunroof_open_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/sunroof_close", HTTP_GET, http_sunroof_close_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/sunroof_vent", HTTP_GET, http_sunroof_vent_handler, NULL});

    // Lights
    httpd_register_uri_handler(server, &(httpd_uri_t){"/headlights_on", HTTP_GET, http_headlights_on_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/headlights_off", HTTP_GET, http_headlights_off_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/fog_on", HTTP_GET, http_fog_on_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/fog_off", HTTP_GET, http_fog_off_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/interior_on", HTTP_GET, http_interior_on_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/interior_off", HTTP_GET, http_interior_off_handler, NULL});

    // Climate
    httpd_register_uri_handler(server, &(httpd_uri_t){"/ac_on", HTTP_GET, http_ac_on_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/ac_off", HTTP_GET, http_ac_off_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/fan_up", HTTP_GET, http_fan_up_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/fan_down", HTTP_GET, http_fan_down_handler, NULL});

    // Audio
    httpd_register_uri_handler(server, &(httpd_uri_t){"/audio_mute", HTTP_GET, http_audio_mute_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/audio_unmute", HTTP_GET, http_audio_unmute_handler, NULL});

    // Trunk
    httpd_register_uri_handler(server, &(httpd_uri_t){"/trunk_open", HTTP_GET, http_trunk_open_handler, NULL});

    // Horn
    httpd_register_uri_handler(server, &(httpd_uri_t){"/horn", HTTP_GET, http_horn_handler, NULL});

    // Hazards
    httpd_register_uri_handler(server, &(httpd_uri_t){"/hazards_on", HTTP_GET, http_hazards_on_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/hazards_off", HTTP_GET, http_hazards_off_handler, NULL});

    // Defrost
    httpd_register_uri_handler(server, &(httpd_uri_t){"/defrost_on", HTTP_GET, http_defrost_on_handler, NULL});
    httpd_register_uri_handler(server, &(httpd_uri_t){"/defrost_off", HTTP_GET, http_defrost_off_handler, NULL});

    ESP_LOGI(TAG_HTTP, "HTTP server started on port %d", config.server_port);
    return server;
}
