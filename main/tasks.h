#ifndef TASKS_H
#define TASKS_H

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
 */
void start_can_rx_task(void);

#ifdef __cplusplus
}
#endif

#endif