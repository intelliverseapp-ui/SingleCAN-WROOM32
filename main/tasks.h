#ifndef TASKS_H
#define TASKS_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Start the CAN RX forward task.
// This task receives CAN frames and logs them.
void start_can_rx_task(void);

// Start the TCP server task.
// This task handles incoming TCP clients and commands.
void start_tcp_server_task(void);

#endif // TASKS_H
