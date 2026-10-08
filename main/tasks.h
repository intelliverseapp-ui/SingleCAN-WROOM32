#ifndef TASKS_H
#define TASKS_H

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Starts the TWAI receive-drain task and TWAI health-monitor task.
 *
 * The receive-drain task prevents the TWAI receive queue from
 * filling, but does not log, serialize, queue, or forward raw CAN
 * frames. PCAN hardware and PCAN-Explorer 7 remain responsible for
 * vehicle CAN capture and decoding.
 *
 * The health-monitor task handles TWAI alerts and bus-off recovery.
 *
 * Successful return requires both tasks to confirm startup and the
 * health task to configure TWAI alerts.
 *
 * Returns ESP_OK when CAN supervision is ready, or an ESP-IDF error
 * when task creation, asynchronous initialization, or the bounded
 * readiness wait fails.
 */
esp_err_t start_can_rx_task(void);

/**
 * Returns true only while the supervised TWAI subsystem is in the
 * RUNNING health state.
 *
 * This exposes no CAN identifier, payload, or transmission API.
 */
bool singlecan_tasks_is_twai_running(void);

#ifdef __cplusplus
}
#endif

#endif