# SingleCAN-WROOM32 Project Analysis

## 1. High-level overview of the project architecture

This repository is a compact ESP32 firmware project centered on a CAN bus bridge and a small control plane exposed over Wi‑Fi and TCP. The architecture is intentionally simple and modular, with a clear startup flow in [main/main.c](main/main.c):

- initialize the onboard LED system
- initialize the outbound TCP queue
- initialize the TWAI/CAN driver
- start the CAN RX task
- start the Wi‑Fi AP
- start the HTTP server
- start the TCP server
- then remain idle while the runtime tasks handle traffic

The major components are:

- [main/main.c](main/main.c): application entry point and boot sequencing
- [main/singlecan_can.c](main/singlecan_can.c): TWAI/CAN driver init, transmit, receive, enable/disable logic
- [main/tcp_server.c](main/tcp_server.c): raw TCP server, connection handling, and command parser
- [main/tcp_queue.c](main/tcp_queue.c): asynchronous outbound queue used to forward CAN frames to the active TCP client
- [main/wifi_ap.c](main/wifi_ap.c): ESP32 AP mode setup with an SSID/password configuration
- [main/http_server.c](main/http_server.c): lightweight HTTP control surface exposing GET endpoints for status and actions
- [main/singlecan_commands.c](main/singlecan_commands.c): high-level vehicle command stubs
- [main/tasks.c](main/tasks.c): task creation wrappers for CAN RX and TCP listener tasks
- [main/singlecan_leds.c](main/singlecan_leds.c): indicator-state mapping for CAN, Wi‑Fi, and TCP activity

The runtime model is a classic FreeRTOS-style producer/consumer design:

- CAN RX task consumes frames from the TWAI peripheral
- received frames are formatted into a text string and pushed into the TCP outbound queue
- a dedicated queue task serializes outbound transmission to the currently connected TCP client
- the TCP server accepts one client, handles commands, and sends acknowledgements
- HTTP endpoints trigger helper functions and return simple strings without a full browser UI

This makes the project more of a hardware telemetry/control bridge than a general application framework. The design is small and understandable, but it is still a prototype rather than a hardened production service.

## 2. Evaluation of code quality, structure, and maintainability

### Strengths

- Clear separation of responsibilities across files and modules.
- Startup flow is easy to follow in [main/main.c](main/main.c).
- Simple FreeRTOS task model is understandable for a small embedded firmware.
- The use of a queue for outbound TCP traffic is a sensible decoupling pattern for CAN RX bursts.
- The project uses ESP-IDF idioms appropriately for Wi‑Fi, HTTP server, and TWAI access.
- Logging is generally consistent and readable.

### Weaknesses

- State is spread across globals rather than a central state machine, which increases hidden coupling.
- Many modules depend on global variables defined in separate translation units, e.g. `g_can_enabled` and `g_tcp_client_sock`, making the system harder to reason about and test.
- In several areas, the code is written as a prototype with “TODO”/placeholder behavior rather than finished control logic.
- The HTTP server implements many routes but the underlying CAN commands are not actually mapped to real CAN frames; they are logged-only stub functions.
- There are no tests or validation harnesses in the repo. The firmware is essentially unverified beyond compile-time assumptions and ad hoc runtime behavior.
- Hardcoded configuration values (SSID, password, port numbers, AP mode) reduce configurability and portability.
- There is no proper configuration layer, no secure secret management, and no runtime status/state model.

### Maintainability assessment

The project is maintainable in a narrow sense because the file layout is approachable and the code footprint is modest. However, maintainability degrades quickly as the project grows because:

- global state is used across modules
- the control plane is not centralized
- command semantics are not strongly typed or validated
- no abstraction layer exists for vehicle-specific CAN message definitions
- there are no tests or boot-time health checks

In short, the code is easy to read today, but it is not yet robust enough to support complex features or long-lived deployment without a fuller state model and stronger validation.

## 3. Identification of potential bugs, risks, or failure points

### 1. High-level command endpoints are stubs, not actual command implementations

In [main/singlecan_commands.c](main/singlecan_commands.c), functions such as `singlecan_cmd_lock_doors()`, `singlecan_cmd_windows_down()`, `singlecan_cmd_ac_on()`, and many others only emit log lines. They do not transmit actual CAN frames. This means the system presents a rich command interface, but it does not actually perform the requested vehicle actions.

This is a major functional gap and a misleading API contract.

### 2. CAN RX formatting reads beyond the active DLC

In [main/singlecan_can.c](main/singlecan_can.c), the formatted log string always prints 8 bytes:

- `msg->data[0] ... msg->data[7]`
- regardless of the actual `msg->data_length_code`

Similarly, in [main/tasks.c](main/tasks.c), the log path prints all 8 bytes even when the frame length is smaller. If the DLC is less than 8, the remaining bytes are not guaranteed to be initialized and may contain stale data. This can produce misleading or invalid CAN telemetry output.

### 3. Queue-based telemetry can silently drop data without visibility

In [main/tcp_queue.c](main/tcp_queue.c), `xQueueSend(..., 0)` is used with no wait time. If the queue is full, the code logs a warning and drops the message. This is sensible for a best-effort telemetry buffer, but it means:

- frames can be lost silently
- no backpressure signal reaches the CAN receive side
- the system may look healthy even while losing valuable data

### 4. Cross-task global state is not protected

The project uses globals such as `g_can_enabled` and `g_tcp_client_sock` across tasks and modules.

This creates typical embedded concurrency hazards:

- unsynchronized reads/writes across FreeRTOS tasks
- stale values across CPU cores or context switches
- race conditions when a socket disconnect overlaps with a queue send

The code is currently small enough to hide this risk, but it is not safe as a long-term design.

### 5. `safe_send()` can block or fail in ways that are not fully handled

In [main/tcp_server.c](main/tcp_server.c), `safe_send()` loops until all bytes are sent. If the client stalls or the network hangs, this can block for a prolonged time. Because the queue consumer also closes the socket on send failure, there is a risk of a stuck or delayed send path affecting overall telemetry flow.

### 6. Hardcoded Wi‑Fi state is a misleading status model

The root HTTP handler in [main/http_server.c](main/http_server.c) returns:

- `"wifi":"connected"`

This is static and not based on actual AP or STA link state. In AP mode, the real state is “AP active,” not “connected” in the client sense. This can create a false impression in the UI or monitoring layer.

### 7. Single-client TCP listener constrains deployment

The TCP server in [main/tcp_server.c](main/tcp_server.c) only accepts one client at a time via `listen(..., 1)`. It also blocks in `accept()` and then processes each client in a nested loop. This is a valid minimal design, but it means:

- only one client can consume telemetry at once
- other clients are refused while the current client stays connected
- a single bad client can monopolize the control path and degrade all others

### 8. There is no robust error recovery or watchdog loop for runtime faults

The project does have some fatal loops for initialization failure (`while (1) vTaskDelay(...)`), but after the system is running it does not maintain richer health checks. For example:

- no watchdog or heartbeat timeout for CAN
- no TCP client health checks beyond socket closure
- no AP client count/state tracking
- no recovery strategy for a dying Wi‑Fi or stale client queue

## 4. Analysis of concurrency, networking, and transport logic

The project uses FreeRTOS tasks in a straightforward way:

- `start_can_rx_task()` creates a task that loops on `singlecan_receive()`
- `start_tcp_server_task()` creates a task that runs the TCP listener
- `tcp_queue_init()` creates a separate queue consumer task for outbound TCP transmission

This is generally valid for a small embedded firmware, but the architecture is lightly synchronized:

- the queue protects outbound data from the sender side to the queue consumer
- the TCP socket state is global and shared without synchronization
- the CAN enable/disable flag is a global bool shared by multiple components

### Transport behavior

The TCP path is simple and direct:

- server listens on port 1234
- accepts a client socket
- receives newline-delimited commands
- commands are parsed cases-insensitively from a buffer
- responses are sent back over the same socket

Telemetry direction:

- CAN frames are received
- formatted into text lines
- pushed onto `tcp_outbound_queue`
- queue consumer drains and sends to the active TCP client

This is a workable design for a prototype, but it relies heavily on a single connected client and does not incorporate any TCP-level reliability strategy beyond a basic send loop.

### Networking observations

- The listener binds to `INADDR_ANY` on port 1234.
- The HTTP server binds to port 80 with `httpd_start()`.
- The device acts as an AP rather than a client, so the project is designed for local LAN access, not internet access.
- The AP is created in software using `esp_wifi_set_mode(WIFI_MODE_AP)`, which is appropriate for the project’s intended local access pattern.

### Concurrency risk summary

The project’s concurrency model is not unsafe in the simplest sense, but it is fragile because key state is not encapsulated. The most important risk is the cross-task sharing of `g_tcp_client_sock` and `g_can_enabled` without mutexes or a formal state/event mechanism.

## 5. Review of CAN frame parsing, sending, and receiving logic

### Sending

The CAN transmit path in [main/singlecan_can.c](main/singlecan_can.c) is relatively defensive:

- rejects transmission if CAN is disabled
- rejects lengths greater than 8
- rejects a NULL pointer for data
- builds a `twai_message_t` and calls `twai_transmit()`

This is a good baseline implementation. It is a narrow but correct API for raw payload transmission.

However, there are some limitations:

- Only standard 11-bit CAN IDs are accepted by default since `extd = 0` is hardcoded.
- Extended frame support is not implemented.
- There is no validation of ID range beyond `uint32_t` type and raw transmission.
- Raw send commands rely on the parser in [main/tcp_server.c](main/tcp_server.c) and are not mapped to real vehicle behaviors yet.

### Receiving

The CAN receive path does the following:

- calls `twai_receive()` with a timeout
- returns on timeout or other error status
- sets LED activity
- formats a log line like `CAN_RX %lu %u ...`
- pushes the string onto the TCP queue

This is a minimal telemetry bridge rather than a structured CAN decoder. There is no:

- frame classification
- filtering by message ID
- decode/parse logic for vehicle signals
- time stamping or buffering
- robust payload inspection beyond text logging

### Parsing quality

The command parser in [main/tcp_server.c](main/tcp_server.c) accepts a `SEND` command formatted like:

- `SEND <id> <dlc> <byte0> ... <byte7>`

It parses up to 10 integer values using `sscanf()`, validates `dlc <= 8`, and then transmits the payload. This is a functional start, but it is not robust as a production protocol:

- it accepts values with no strong schema validation
- it does not support structured frame metadata or extended IDs
- it does not validate against known CAN message sets
- it can produce confusing behavior on malformed input

### Key issue: formatting beyond valid DLC

The biggest code-level issue is that the program logs all eight payload bytes regardless of the valid frame length. This is a correctness problem in the telemetry path and can distort data interpretation.

## 6. Review of TCP reconnect logic and exponential backoff behavior

This project does not implement a reconnect strategy in the usual sense.

### Observed behavior

When the client disconnects, the code does this:

- `recv()` returns <= 0
- logs “Client disconnected”
- toggles the LED state
- closes the socket
- resets `g_tcp_client_sock = -1`
- loops back to `accept()`

This is a simple reconnect loop, but it is not a deliberate reconnect policy. There is no:

- exponential backoff
- jitter
- retry timer
- reconnect scheduling
- network state tracking
- client-availability polling
- Wi‑Fi reconnect management

### Why this matters

On a flaky or lossy wireless link, immediate re-accept attempts can create a tight reconnect loop. Without any backoff, an unstable client or intermittent network can trigger repeated connection churn. That may create CPU churn, log churn, and socket resource pressure.

### Relevant conclusion

The code has a reconnect loop, but not a robust reconnect algorithm. There is no exponential backoff behavior anywhere in the repo. This is a notable weakness for a system expected to operate over a wireless link.

## 7. Review of UI state handling and event flow

There is no conventional visual UI or stateful front-end. Instead, the project uses an HTTP control plane and indicator LEDs as the effective “UI.”

### Control flow

- HTTP endpoints in [main/http_server.c](main/http_server.c) trigger command handlers.
- These handlers call functions in [main/singlecan_commands.c](main/singlecan_commands.c) or toggling functions in [main/singlecan_can.c](main/singlecan_can.c).
- A few routes are functional status checks, but most are placeholders.
- The root `/` endpoint exposes JSON state with `can_enabled` and a `tcp_client_connected` boolean.
- The LED subsystem conveys rough runtime status for CAN activity, Wi‑Fi AP state, and TCP status.

### State model observations

The practical state is a minimal combination of:

- `g_can_enabled` boolean
- `g_tcp_client_sock` integer socket handle
- LED flashes for indication

This is not a formally modeled state machine. There is no dedicated state enum, no event queue, and no explicit transitions around connection loss, permission changes, network state, or device health.

### Event-flow issues

- The root JSON status uses a hardcoded `"wifi":"connected"`, which does not actually represent the true AP or client link state.
- UI/API state is not synchronized with real network or hardware state.
- The system can claim to have a connected client while the actual socket is stale or the queue is dropping data.
- Since many endpoints are stubs, a client can receive success responses while no real action occurs.

## 8. Security considerations relevant to TCP, Wi‑Fi AP mode, and command injection

### 1. Hardcoded AP credentials

In [main/wifi_ap.c](main/wifi_ap.c):

- SSID: `SingleCAN-WROOM32`
- password: `singlecan123`

These are embedded directly in source and also logged at startup. This is a significant operational security issue because:

- anyone who knows the project can recover the password from the binary or logs
- the AP is discoverable and guessable
- an attacker can connect to the local network and reach exposed control interfaces

### 2. AP mode is local and open by design

AP mode is intentionally used for convenience, but the implementation does not include:

- authentication at the HTTP layer
- access control based on device identity
- TLS/HTTPS
- session management
- rate limiting

This means any nearby client that connects to the AP can potentially reach the TCP server or the HTTP endpoints.

### 3. Unauthenticated network commands can trigger CAN actions

The TCP server processes raw text commands such as `PING`, `ENABLE_CAN`, `DISABLE_CAN`, `STATUS`, and `SEND ...` without any authentication or authorization. The HTTP server also exposes direct GET endpoints that can invoke CAN-related logic. If an attacker connects to the AP, they can potentially control the device and send raw CAN frames.

Even though the system is not directly executing a shell command, it is effectively exposing a remote actuator surface to the CAN bus.

### 4. No TLS or encrypted transport

The HTTP server is plain HTTP on port 80. The TCP socket is also plain TCP, not TLS or any authentication layer. This makes the control plane vulnerable to interception and tampering on the local Wi‑Fi network.

### 5. No command injection in the shell sense, but there is a high-risk control-surface issue

There is no use of `system()`, `popen()`, or shell command execution in the code paths reviewed. In that strict sense, there is no direct shell command injection. However, there is still an important command-injection equivalent risk: the application accepts text commands from the network and interprets them as application actions, including CAN control. That is a security boundary issue, not a classic OS injection issue.

### 6. CAN payload manipulation and safety

Because raw CAN frames can be sent remotely, the system can drive the bus with arbitrary IDs and byte payloads. This is dangerous if the device is connected to a vehicle network or any sensitive hardware. There is no allowlist, validation against a CAN message dictionary, or safety gating.

## 9. List of recommended improvements (analysis only — no code changes)

1. Implement a real state machine for device health, AP state, socket state, and CAN state.
2. Add robust synchronization around shared globals using mutexes or event-driven state ownership.
3. Replace hardcoded Wi‑Fi credentials with configuration or secure provisioning.
4. Add proper reconnect logic with exponential backoff and jitter for TCP and Wi‑Fi recovery.
5. Add client connection tracking, heartbeat checks, and timeouts for idle sockets.
6. Fix CAN frame formatting to respect `data_length_code` and avoid printing uninitialized payload bytes.
7. Add structured CAN message parsing and support a known message dictionary instead of raw text logging only.
8. Implement real commands for vehicle operation instead of stubbed log-only handlers.
9. Add authentication and authorization to TCP and HTTP control endpoints.
10. Switch to HTTPS or encrypted transport if the device is exposed beyond trusted local access.
11. Add rate limiting and input validation for network commands.
12. Provide telemetry loss detection and counters for dropped CAN frames.
13. Add a watchdog for CAN health and AP connectivity.
14. Add tests or a simulation harness for CAN, TCP, and HTTP flows.
15. Add explicit runtime config in Kconfig or NVS rather than compile-time hardcoding.

## 10. Final summary

This project is a compact, understandable ESP32 CAN bridge with an AP-mode Wi‑Fi network, a raw TCP command interface, and a simple HTTP control surface. Its overall architecture is reasonable for a prototype or proof-of-concept, and the code structure is modular enough to follow.

However, the code is not yet production-grade. The most important concerns are:

- misleading or incomplete status reporting
- raw global state shared across tasks without formal synchronization
- no real reconnect/backoff strategy
- log-only command stubs instead of actual CAN actions
- hardcoded Wi‑Fi credentials and unauthenticated network control
- lack of safety gating around CAN bus payloads
- missing structured CAN decoding and robust telemetry handling

In short, the project demonstrates a viable embedded architecture and control-plane concept, but it still needs significant work before it can be considered secure, reliable, and operationally safe in a real vehicle or sensitive network environment.
