# SingleCAN-WROOM32 Copilot Review

## Executive Summary

The repository is a small ESP-IDF firmware project with Bluetooth Classic SPP, a cJSON command dispatcher, a TWAI receive task, and a bounded outbound FreeRTOS queue. The command functions currently log TODO messages and do not transmit vehicle-specific CAN frames. The inspected command dispatcher contains no brake, steering, airbag, torque, transmission, traction, eco, powertrain, or cruise-control command path.

The code is **not ready for passive Honda Accord CAN capture**. SPP treats each callback as a whole command rather than a byte stream; JSON IDs and complete schemas are not enforced; the "ok" response describes a recognized stub, not an executed vehicle action; SPP writes from the response and telemetry paths are not serialized according to the ESP-IDF API contract; and TWAI is started in normal mode rather than listen-only. The saved boot log also confirms a 4 MB flash chip with a firmware image configured for 2 MB.

Finding count: **0 Critical, 4 High, 9 Medium, 3 Low**.

## Architecture Assessment

- `app_main()` initializes NVS and LEDs, creates the outbound queue task, starts TWAI and its receive task, then initializes Bluetooth. It idles afterward.
- `bt_spp.c` starts an SPP server named `BabyNodeCAN`. Its callback passes each received data indication directly to `singlecan_commands_process()`.
- `singlecan_commands.c` parses cJSON objects and maps recognized command strings to log-only stubs. It also accepts a raw legacy command string after JSON parsing fails.
- `singlecan_can.c` configures the TWAI controller at 500 kbit/s, in normal mode, with an accept-all filter. The public raw-send API can submit a standard-format frame, but the current command stubs do not call it.
- `singlecan_can.c` formats received frames as `CAN_RX ...` text and queues them; `tcp_queue.c` uses SPP despite its TCP naming.
- The workspace does not contain the Android/BabyNodeAutomotive frontend, partition CSV, or test sources. Frontend command compatibility therefore cannot be proven here.
- `PROJECT_ANALYSIS.md` describes older Wi-Fi, HTTP, and TCP modules that are not present in the current build.

## Critical Findings

None identified in the inspected source and configuration.

## High Findings

### H1 — SPP receive path does not frame a byte stream

- **Severity:** High
- **Exact file and function:** `main/bt_spp.c` — `spp_event_handler()` (`ESP_SPP_DATA_IND_EVT`); `main/singlecan_commands.c` — `singlecan_commands_process()`, `process_json_packet()`
- **Problem:** Each data indication is copied into a fresh 256-byte buffer (at most 255 bytes) and immediately parsed as one command. There is no per-connection accumulator, newline scanning, or oversize-frame discard state. Oversized data is silently truncated and still passed for parsing. Embedded NUL bytes terminate the C string early. `cJSON_Parse()` is not asked to verify that the whole framed input was consumed.
- **Concrete failure scenario:** One JSON command split across two callbacks fails to parse and is not reassembled. Two newline-delimited commands coalesced into one callback are not dispatched individually; the parser can process the first JSON value and ignore trailing bytes. A valid command followed by a NUL and extra input can be treated as a valid prefix. An oversized frame may be truncated and then misclassified or parsed as a different prefix. Commands without a newline are nevertheless attempted immediately, so behavior depends on the SPP packet boundary rather than the documented line protocol.
- **Recommended correction:** Implement bounded per-connection newline framing. Process every complete line in each callback, retain incomplete bytes between callbacks, reject embedded NUL/binary data, and discard an overlong frame through its delimiter without parsing a truncated prefix.
- **Required verification test:** Feed one JSON command split at every byte boundary; multiple commands in one callback; partial lines with no newline; exact-limit and over-limit frames; embedded NUL/binary bytes; malformed JSON; and a valid command followed by trailing data. Assert exact dispatch and response counts.

### H2 — `status="ok"` reports a stub as a successful vehicle action

- **Severity:** High
- **Exact file and function:** `main/singlecan_commands.c` — `process_json_packet()`, `dispatch_command()`, and `singlecan_cmd_*()` stubs
- **Problem:** Every implemented vehicle handler only emits a TODO log. `dispatch_command()` returns `true` for these handlers, which causes `process_json_packet()` to send `status="ok"`. Module configuration also returns success after logging a value without changing the active configuration. The response does not mean CAN transmission, vehicle acceptance, or verified execution.
- **Concrete failure scenario:** Android sends `locks.all.lock`; the stub logs “TODO: map to CAN frame,” but the backend responds `ok`. A client or operator can conclude that the vehicle was locked when no CAN frame was sent.
- **Recommended correction:** Define explicit acknowledgment semantics. Until a real implementation exists, respond with a distinct `unsupported`/`not_implemented` result (or an explicitly named `accepted` state if that is the agreed protocol). For real operations, propagate the transmit result and do not report execution before the corresponding result is known.
- **Required verification test:** Exercise every command while handlers are stubs and assert none returns a success state that implies execution. With a fake CAN/transmit result, assert acknowledgments reflect acceptance, failure, and completion semantics exactly.

### H3 — SPP writers bypass the required completion/congestion serialization

- **Severity:** High
- **Exact file and function:** `main/bt_spp.c` — `bt_spp_send()`, `spp_event_handler()`; `main/tcp_queue.c` — `tcp_queue_task()`
- **Problem:** `bt_spp_send()` tracks one in-progress write and congestion, but the queue task calls `esp_spp_write()` directly and bypasses both checks. Both paths can write the same connection concurrently. The queue task removes each item regardless of asynchronous completion and logs “Sent” when the API only accepted the request. Write failures are only logged; congested/failed telemetry is dropped. ESP-IDF 5.3.1 explicitly recommends waiting for `ESP_SPP_WRITE_EVT` and, when congested, `ESP_SPP_CONG_EVT` with `cong == false` before issuing the next write.
- **Concrete failure scenario:** A CAN telemetry write is outstanding when a command response arrives. The response path does not know about that write and may issue another write; subsequent congestion or completion can cause a dropped response or telemetry item, and a completion from one path clears the shared flag for another.
- **Recommended correction:** Route all SPP output through one connection-scoped writer/queue. Permit one outstanding write, resume only on the correct write/congestion event, match callbacks to the active handle, and retain or explicitly count/report items that fail.
- **Required verification test:** With mocked SPP events, concurrently submit responses and telemetry; inject immediate write errors, asynchronous write failures, congestion, delayed uncongestion, disconnect, and reconnect. Assert no overlapping writes and verify exactly which messages are delivered or explicitly dropped.

**Buffer-lifetime note:** The installed ESP-IDF 5.3.1 SPP implementation deep-copies `p_data` in its BTC transfer callback before `esp_spp_write()` returns. Therefore, freeing the serialized response or returning from `tcp_queue_task()` immediately after an accepted call is not, by itself, a caller-buffer lifetime defect for this build. The asynchronous completion and serialization problems above remain.

### H4 — TWAI is active, not passive/listen-only

- **Severity:** High
- **Exact file and function:** `main/singlecan_can.c` — `singlecan_init()`, `singlecan_send()`; `main/singlecan_can.h` — `SINGLECAN_BITRATE`
- **Problem:** Initialization selects `TWAI_MODE_NORMAL`, and `g_can_enabled` starts as `true`. Normal mode allows the controller to participate in bus signaling (including acknowledgments); this does not meet a passive capture requirement. The exposed `singlecan_send()` API accepts arbitrary caller-supplied identifiers and payloads without an application allowlist. No current command stub calls it, so there is no identified SPP-to-CAN transmit path today; nevertheless, the firmware is not configured as a passive observer and has a live raw transmit API for any future caller.
- **Concrete failure scenario:** During a supposedly passive capture, the ESP32 acknowledges observed traffic or, after a future integration invokes `singlecan_send()`, emits an unverified frame onto the vehicle bus. A normal-mode controller may also participate in error signaling rather than remaining silent.
- **Recommended correction:** Use TWAI listen-only mode for capture firmware and keep transmission unavailable in that mode. Before enabling any transmit feature, require verified vehicle-specific mappings, strict ID/DLC/payload validation, and an explicit Phase 1 allowlist.
- **Required verification test:** Inspect the configured TWAI mode on target; verify with an independent CAN analyzer that the node neither acknowledges nor transmits while in capture mode. Unit-test that all transmit entry points reject calls in passive mode and that prohibited/unknown command identifiers cannot reach transmission.

## Medium Findings

### M1 — Firmware is configured for 2 MB despite a recorded 4 MB chip

- **Severity:** Medium
- **Exact file and function:** `sdkconfig` / `sdkconfig.old` — `CONFIG_ESPTOOLPY_FLASHSIZE_2MB`; generated `build/flasher_args.json`; boot behavior in `app_main()`
- **Problem:** Both local SDK configurations and generated flash arguments specify 2 MB. A saved boot log in `build/log/idf_py_stdout_output_70534` reports `Detected size(4096k) larger than the size in the binary image header(2048k). Using the size in the binary image header.` The configuration and recorded hardware size disagree. The local `sdkconfig` is ignored by Git and is not a reproducible project default.
- **Concrete failure scenario:** A 4 MB ESP32 boots an image that advertises 2 MB, causing the runtime to use the smaller size and making the additional flash unavailable to partition/layout decisions. A clean checkout may also build with a different SDK configuration.
- **Recommended correction:** Confirm the actual target flash with a read-only flash-ID check, set the firmware flash size to match, and validate the resulting partition table and image header. Make the intended project defaults reproducible rather than relying only on the ignored local `sdkconfig`.
- **Required verification test:** Compare physical flash ID, image header, `idf.py partition-table`, and generated flashing arguments; perform a boot check with no size mismatch warning and confirm all partition boundaries lie within detected flash.

### M2 — JSON IDs and command schema are not strict

- **Severity:** Medium
- **Exact file and function:** `main/singlecan_commands.c` — `process_json_packet()`
- **Problem:** Missing or nonnumeric `id` only logs a warning; the command still dispatches with response ID 0. `cJSON_IsNumber()` accepts fractional and negative numbers, and `valueint` narrows them to `int` (fractions are truncated and out-of-range values are not rejected by this code). Duplicate object fields and duplicate request IDs are not rejected or deduplicated. The JSON command shape is not validated as a complete schema, and invalid JSON falls through to legacy raw-command dispatch without a protocol response.
- **Concrete failure scenario:** A command with no ID or an ID such as `-2.7` executes and is acknowledged with a transformed/default ID. Retrying a non-idempotent request with an already-used ID invokes its handler again. A malformed JSON string equal to a legacy command can be dispatched without a structured error response.
- **Recommended correction:** Require a unique, integral, nonnegative ID within an agreed range; reject duplicate fields and IDs according to a defined retry policy; validate required field types and allowed fields; and remove or explicitly version the raw legacy fallback. Ensure malformed envelopes receive a well-formed error response when an ID can be safely identified.
- **Required verification test:** Test missing, string, fractional, negative, maximum, overflow, and duplicate IDs; duplicate JSON fields; missing/wrong-type `type`, `command`, and `value`; replayed requests; and invalid JSON that resembles a command.

### M3 — SPP connection state is not associated with event handles

- **Severity:** Medium
- **Exact file and function:** `main/bt_spp.c` — `spp_event_handler()`, `bt_spp_send()`, `bt_spp_get_handle()`, `bt_spp_is_connected()`; `main/tcp_queue.c` — `tcp_queue_task()`
- **Problem:** `ESP_SPP_SRV_OPEN_EVT` replaces one global handle and marks connected without checking whether a prior client is active. `ESP_SPP_CLOSE_EVT` clears the global connection unconditionally, and data/write/congestion events are not checked against the active handle. The queue task reads the global connection state and handle separately, outside the callback, without synchronization.
- **Concrete failure scenario:** If an old connection closes after a new connection opens, the old close event clears the new handle. If data from a stale handle is delivered, it is parsed and its response may be sent to the currently stored handle. A disconnect between the two queue-task reads and the write can use stale state; queued old-session data can also be sent after reconnect.
- **Recommended correction:** Keep connection state owned by one serialized event path or protect it with appropriate synchronization. Validate every event handle, define one-client versus multi-client behavior, invalidate/flush pending output on disconnect, and bind responses to the request's originating session.
- **Required verification test:** Simulate open/close/data/write/congestion events for two different handles, close-old-after-open-new ordering, disconnect during a queued write, and reconnect with pending telemetry. Assert no stale event changes or writes to the active session.

### M4 — TWAI bus-off has no monitored recovery path

- **Severity:** Medium
- **Exact file and function:** `main/singlecan_can.c` — `singlecan_init()`, `singlecan_receive()`; `main/tasks.c` — `can_rx_forward_task()`
- **Problem:** The application does not enable or process TWAI bus-off alerts, inspect TWAI state, or call `twai_initiate_recovery()`. The receive task only logs an error and delays before trying again. `sdkconfig` enables an ESP32 bus-off errata fix, but that does not add application-level bus-off recovery.
- **Concrete failure scenario:** The controller enters bus-off due to wiring, bitrate, or bus errors. The task continues retrying receive, while the capture silently stops or repeatedly logs errors; there is no recovery or clear health state for the operator.
- **Recommended correction:** Enable the relevant TWAI alerts, publish bus state/error counters, initiate recovery when appropriate, and wait for confirmed recovery before treating capture as active again.
- **Required verification test:** Inject or simulate bus-off, verify a bus-off indication and recovery transition, then confirm receive resumes only after recovery. Also test persistent bus errors and ensure the firmware does not claim healthy capture.

### M5 — Module configuration is acknowledged but has no effect or gating

- **Severity:** Medium
- **Exact file and function:** `main/singlecan_commands.c` — `process_module_config()`, `process_json_packet()`, `dispatch_command()`
- **Problem:** `config.module` accepts `single` or `dual` and returns success after logging. No selected configuration is stored, no single/dual behavior is implemented, and ordinary commands are accepted before any module configuration.
- **Concrete failure scenario:** The client configures `dual`, receives `ok`, and then sends commands; firmware behaves exactly as it does for `single`. Alternatively, commands sent immediately after connection are accepted before configuration.
- **Recommended correction:** Either remove the unsupported configuration command or persist a validated module mode and make command acceptance depend on a defined configuration state. Do not acknowledge a configuration change that is not applied.
- **Required verification test:** Test commands before configuration, valid and invalid single/dual values, configuration changes, and boot/reconnect behavior. Verify reported state matches the actual selected hardware path.

### M6 — Outbound CAN telemetry is not framed as newline-delimited JSON

- **Severity:** Medium
- **Exact file and function:** `main/singlecan_can.c` — `singlecan_receive()`; `main/tcp_queue.c` — `tcp_queue_push()`, `tcp_queue_task()`, `tcp_queue_init()`
- **Problem:** CAN events are formatted as plain `CAN_RX ...` text and written with exactly `item.len` bytes, without a newline or JSON envelope. This does not match the newline-delimited JSON framing used for command responses. The queue drops messages when disconnected or when writes fail/congest; queue/task initialization failures only log and return, while `app_main()` continues as if startup succeeded.
- **Concrete failure scenario:** The client cannot parse telemetry with the same NDJSON reader used for responses; adjacent writes may be coalesced. On congestion or a sender-task creation failure, frames disappear with no protocol-level loss count or startup failure indication.
- **Recommended correction:** Define and implement a versioned telemetry envelope and delimiter consistent with the client protocol. Make queue creation/task startup failures visible to application readiness; track dropped telemetry and define reconnect/backpressure behavior.
- **Required verification test:** Validate multiple telemetry records through the real client framing parser; test disconnected periods, full queue, congestion, write failure, queue allocation failure, and task creation failure. Assert records are framed or counted as dropped.

### M7 — Bluetooth command authorization relies only on SPP link authentication

- **Severity:** Medium
- **Exact file and function:** `main/bt_spp.c` — `spp_event_handler()` (`ESP_SPP_INIT_EVT`), `bt_spp_init()`, `ESP_SPP_DATA_IND_EVT`
- **Problem:** The device is made connectable and generally discoverable and starts its service with `ESP_SPP_SEC_AUTHENTICATE`, but the firmware has no peer allowlist or application-level authorization, protocol version negotiation, command rate limit, or replay policy. Link authentication is not equivalent to authorizing a particular client to invoke vehicle commands. Current command functions are stubs, but the control surface is intended to grow into a vehicle actuator.
- **Concrete failure scenario:** A nearby device that can pair/connect (or an unintended already-bonded peer) can send repeated commands; after real CAN mappings are added, an unauthorized peer could invoke vehicle controls.
- **Recommended correction:** Define the pairing/bonding trust model and enforce an authorized peer policy before accepting commands. Add protocol-version negotiation and request freshness/replay controls appropriate to the client, and limit command rates.
- **Required verification test:** Verify an unpaired and unauthorized peer cannot invoke commands, an authorized peer can, replayed IDs are handled as specified, and rate-limited traffic does not starve SPP lifecycle events.

### M8 — Bluetooth startup success is reported before server readiness

- **Severity:** Medium
- **Exact file and function:** `main/bt_spp.c` — `bt_spp_init()`, `spp_event_handler()` (`ESP_SPP_INIT_EVT`, `ESP_SPP_START_EVT`); `main/main.c` — `app_main()`
- **Problem:** `bt_spp_init()` returns after `esp_spp_enhanced_init()` accepts an asynchronous request. `app_main()` then turns the LED green and logs “ready” without waiting for `ESP_SPP_START_EVT`. Failures from setting the device name, setting scan mode, or starting the SPP server are logged in the callback but do not change readiness or trigger recovery.
- **Concrete failure scenario:** SPP initialization returns `ESP_OK`, then server startup fails asynchronously; the LED and log still indicate readiness while Android cannot connect.
- **Recommended correction:** Model controller, Bluedroid, SPP, and server readiness explicitly. Propagate asynchronous failures to application state and indicate ready only after the SPP server-start event succeeds.
- **Required verification test:** Mock successful and failed init/start events, including delayed and missing callbacks. Assert readiness is withheld until the server is started and cleared/reported on failure.

### M9 — No automated tests cover safety-critical protocol paths

- **Severity:** Medium
- **Exact file and function:** Repository-wide — no test source or test component; target seams include `main/bt_spp.c::spp_event_handler()`, `main/singlecan_commands.c::process_json_packet()`, and `main/singlecan_can.c::singlecan_send()`
- **Problem:** No parser, framing, SPP event, queue, reconnect, or CAN-safeguard tests are present. Existing build artifacts do not validate runtime behavior or safety invariants.
- **Concrete failure scenario:** The callback framing defects, duplicate-ID behavior, congestion drops, or accidental CAN transmission can be introduced or remain undetected while the firmware still compiles.
- **Recommended correction:** Add a host-testable parser/framer seam and ESP-IDF Unity or equivalent tests with fake SPP/TWAI operations. Gate future real CAN mappings on required protocol and safety tests.
- **Required verification test:** Automated coverage must include the framing/schema cases listed above, connection and queue event ordering, passive-mode transmit rejection, bus-off handling, and explicit assertions that prohibited commands never reach TWAI.

## Low Findings

### L1 — Project analysis and TCP-era declarations are stale

- **Severity:** Low
- **Exact file and function:** `PROJECT_ANALYSIS.md`; `main/tasks.h` — `start_tcp_server_task()`; `main/singlecan_can.h` — `TAG_SINGLECAN`
- **Problem:** The analysis document describes Wi-Fi AP, HTTP, and TCP server source files and behavior that are not in the current repository or CMake target. `tasks.h` still declares an unimplemented TCP server task. The outbound queue is still named `tcp_queue` even though it sends through SPP. Saved build logs also show an unused `TAG_SINGLECAN` header variable warning.
- **Concrete failure scenario:** A maintainer follows the document or TCP task declaration when tracing runtime behavior and believes old network endpoints or server code exist. The misleading naming can also lead to changes being made to the wrong transport assumptions.
- **Recommended correction:** Update the analysis document to describe the current Bluetooth architecture, remove obsolete declarations when appropriate, and rename or document transport-specific queue terminology consistently.
- **Required verification test:** Compare documentation and declared APIs with the actual CMake source list and linked symbols; ensure no documentation references nonexistent implementation files as active.

### L2 — CAN receive logs include bytes beyond the reported DLC

- **Severity:** Low
- **Exact file and function:** `main/singlecan_can.c` — `singlecan_receive()`; `main/tasks.c` — `can_rx_forward_task()`
- **Problem:** Both formatters print all eight `msg->data` bytes regardless of `data_length_code`. The indices remain inside the fixed data array, so this is not an out-of-bounds read, but bytes beyond DLC are not meaningful frame payload and may be stale or misleading.
- **Concrete failure scenario:** A frame with DLC 1 is logged with seven extra bytes, and a capture operator mistakes those bytes for data present on the bus.
- **Recommended correction:** Format exactly the bytes indicated by DLC and validate the DLC before formatting.
- **Required verification test:** Feed frames with DLC 0, 1, 7, and 8 and assert the output contains exactly the valid number of payload bytes.

### L3 — Raw command and value contents are logged

- **Severity:** Low
- **Exact file and function:** `main/bt_spp.c` — `spp_event_handler()`; `main/singlecan_commands.c` — `send_response()`, `process_json_packet()`, `singlecan_commands_process()`
- **Problem:** Complete inbound packets, command values, and serialized responses are logged at info level. This is format-string-safe as written, but may expose client-supplied identifiers or future sensitive protocol data on the debug console and can create log volume from repeated requests.
- **Concrete failure scenario:** A captured console log contains command values or identifiers that should not be retained, or repeated malformed input fills the available log sink.
- **Recommended correction:** Log structured event metadata and status rather than full payloads by default; reserve bounded, explicitly enabled payload diagnostics for development.
- **Required verification test:** Send commands containing test secrets and malformed/high-volume input; verify default logs omit payload contents and rate/size limits prevent unbounded log output.

## Bluetooth SPP Assessment

- Bluetooth Classic controller, Bluedroid, callback registration, and enhanced SPP initialization are called in the expected broad order for the configured Classic-only device. BLE memory is released before controller initialization, and saved `sdkconfig` enables Classic BT and SPP.
- The SPP server is configured with `ESP_SPP_SEC_AUTHENTICATE`; the device is also made connectable and generally discoverable. This is not application-level peer authorization.
- Receive fragmentation, coalescing, newline framing, oversized input, and embedded NUL handling are not robust; see H1.
- Connection state is represented by unsynchronized globals and event handles are not correlated; see M3.
- There are two independent write producers and no completion-driven single writer; see H3. `ESP_SPP_WRITE_EVT` logs failures but does not associate them with a request or retained message.
- The currently installed ESP-IDF 5.3.1 SPP implementation deep-copies buffers submitted to `esp_spp_write()`, so the immediate caller-buffer free/stack lifetime is acceptable for this build.
- `bt_spp_init()` does not wait for asynchronous server-start success before the main task indicates readiness; see M8.
- Android reconnection behavior cannot be checked without the Android repository. On ESP32 reboot, startup reinitializes the server, but there is no backend session/replay state to coordinate with a reconnecting client.

## JSON and Command Protocol Assessment

- The parser expects an object with a string `type` equal to `command`, a string `command`, and optionally a string `value`. It does not require a valid `id`; it substitutes 0 and still dispatches.
- IDs are not restricted to nonnegative integers or an agreed range. Fractional and negative JSON numbers pass `cJSON_IsNumber()`, then `valueint` is used. Duplicate/replayed IDs are not detected.
- Successful command responses are sent after dispatch, but dispatch success currently means only “recognized and stub function called,” not CAN execution (H2).
- Invalid JSON falls back to the legacy raw command dispatcher, which produces no structured acknowledgment. Parsing does not sit behind a proper line framer (H1).
- The dispatcher currently supports comfort/body-type actions including locks, windows, sunroof, lights, climate, audio, trunk, horn, hazards, and defrost. It does not contain a path for the specifically excluded brake, steering, airbag, engine torque, transmission, traction-control, eco/powertrain, or cruise-control commands.
- The code has uppercase and dotted aliases and compares case-insensitively, but frontend/backend compatibility is unverified because BabyNodeAutomotive is absent from this workspace. There is no protocol version field or negotiation.
- `config.module` accepts `single` and `dual` but changes no behavior, and ordinary commands are not gated on configuration (M5).

## FreeRTOS and Queue Assessment

- The CAN receive task and outbound queue task each use a 4096 stack depth and loop indefinitely. No task shutdown/lifecycle mechanism is provided; runtime is designed as a permanent firmware process.
- The outbound queue is bounded (64 records of 256 bytes) and queue sends are nonblocking. Full-queue behavior drops data and turns on the error LED. Disconnected and failed-write telemetry is also dropped.
- Queue/task creation failures are logged but are not propagated to `app_main()`, so Bluetooth can be marked ready with telemetry unavailable (M6/M8).
- The queue decouples CAN receive from the SPP sender, but the sender does not honor SPP completion/congestion serialization and the telemetry format is not NDJSON (H3/M6).
- Shared SPP handle/connected/congested/write state crosses the SPP callback and queue task without a lock or single-owner message path (M3).
- `g_can_enabled` is a global ordinary `bool`; currently it is only initialized true and changed through public enable/disable APIs, but it has no synchronization if future tasks call those APIs concurrently.

## TWAI and CAN Safety Assessment

- TWAI uses GPIO 5 for TX, GPIO 4 for RX, a hardcoded 500 kbit/s timing macro, normal mode, and an accept-all filter. No single/dual module selection affects this setup.
- `g_can_enabled` begins true. The public `singlecan_send()` validates DLC up to 8 and checks for a null payload when DLC is nonzero, but it does not apply a vehicle message allowlist or explicit 11-bit identifier range check. It sets `extd = 0`. No current SPP command stub invokes it.
- `singlecan_receive()` has a 10 ms receive timeout and forwards raw text rather than decoded frames. It and the CAN task log eight data bytes for every received frame, irrespective of DLC.
- There is no application bus-off monitoring or recovery flow; see M4. The build's TWAI errata Kconfig flags are not a substitute for recovery logic.
- No CAN ID/payload mapping for the 2014 Accord exists, which is appropriate while passive capture and verification are pending. Do not add guessed or placeholder frames.
- For passive PCAN-Explorer 7 work, use listen-only mode, verify adapter wiring/ground and bus bitrate with the analyzer, confirm the ESP32 cannot acknowledge or transmit, then capture and label traffic without replaying it.

## Security Assessment

- The Bluetooth device is discoverable/connectable and relies on SPP authentication without a firmware-level authorized-peer policy. There is no command freshness or replay defense, protocol versioning, or request-rate policy (M7).
- This repository does not contain Wi-Fi/TCP/HTTP command endpoints or hardcoded Wi-Fi credentials; those claims in `PROJECT_ANALYSIS.md` are stale.
- Inbound packet/command/value contents are logged. Logging uses fixed format strings, so the inspected code does not expose a format-string vulnerability; payload disclosure and log flooding remain concerns (L3).
- There is no current command-dispatch path to raw CAN transmission, but the raw send API is publicly exposed within the firmware and the controller is in normal mode (H4). Security and safety gates must precede any real CAN command mapping.
- No source evidence was found for a prohibited safety-critical command path. This finding is scoped to the code in this repository and does not establish safety of future mappings or external frontend behavior.

## Dead or Legacy Code

- `PROJECT_ANALYSIS.md` refers to `tcp_server.c`, `wifi_ap.c`, and `http_server.c`, which are absent from the current repository and not compiled.
- `start_tcp_server_task()` is declared in `main/tasks.h` but has no implementation or call site.
- `tcp_queue.*` and `TCP_QUEUE_*` names remain although the implementation is now Bluetooth SPP telemetry.
- `singlecan_enable_can()`, `singlecan_disable_can()`, `singlecan_get_status()`, and `singlecan_stop()` are defined but have no caller in the current source.
- `TAG_SINGLECAN` is a static header variable and saved build output reports an unused-variable warning.
- The `singlecan_commands.h` comment describes high-level “safe stubs”; those stubs are indeed non-transmitting today, but the `ok` protocol status does not communicate their stub state.

## Missing Tests

No test source, Unity test component, host test harness, or protocol fixture is present. In particular, tests are missing for:

- Fragmented/coalesced SPP data indications, newline parsing, no-newline input, oversize recovery, binary input, and embedded NUL.
- cJSON type/range validation, missing and duplicate IDs/fields, duplicate request replay, unknown commands, and legacy fallback behavior.
- SPP open/close/write/congestion event ordering, simultaneous response/telemetry producers, stale handles, disconnect, and reconnect.
- Queue full/allocation/task-creation failures, disconnected drops, telemetry framing, and loss reporting.
- TWAI listen-only behavior, ID/DLC validation, bus-off recovery, and proof that prohibited/unknown commands cannot transmit.
- Stub acknowledgment accuracy and future distinction among accepted, transmitted, and executed states.

## Honda Accord CAN-Mapping Readiness Checklist

| Readiness item | Assessment |
|---|---|
| Reliable fragmented and coalesced SPP framing | **Fail** — callback boundaries are treated as command boundaries; see H1. |
| Bounded input buffers | **Partial** — fixed 255-byte callback copy exists, but there is no bounded per-line accumulator or safe oversize-frame recovery. |
| Strict JSON schema validation | **Fail** — ID and complete envelope validation are insufficient; see M2. |
| Correct cJSON cleanup | **Pass** — parsed roots and serialized response buffers are freed on the inspected paths. |
| Serialized and lifetime-safe SPP writes | **Fail overall** — ESP-IDF deep-copies caller buffers, but multiple producers do not serialize or honor completion/congestion; see H3. |
| Accurate acknowledgments | **Fail** — `ok` is returned for log-only stubs; see H2. |
| Stable connection-handle lifecycle | **Fail** — event handles and cross-task state are not correlated/synchronized; see M3. |
| Phase 1 allowlist | **Partial** — current dispatcher lists comfort/body commands, but legacy raw fallback and no explicit policy table weaken enforcement. |
| Safety-critical commands excluded | **Pass for current dispatcher** — no brake, steering, airbag, torque, transmission, traction, eco/powertrain, or cruise command is present. |
| No live placeholder CAN frames | **Pass for current command path** — stubs do not call `singlecan_send()`; raw transmit API remains available to internal callers. |
| TWAI passive/listen-only readiness | **Fail** — configured `TWAI_MODE_NORMAL`; see H4. |
| Bus-off handling | **Fail** — no application recovery path; see M4. |
| Frontend/backend command compatibility | **Unverified** — frontend repository is absent; aliases and protocol responses cannot be compared. |
| Test readiness | **Fail** — no tests or harness are present; see M9. |
| Passive Honda Accord capture readiness | **Fail** — do not connect this firmware to the vehicle for passive capture until listen-only mode and end-to-end framing/readiness are verified. |

## Prioritized Remediation Plan

1. Before any vehicle connection, build a capture-only image in TWAI listen-only mode and verify with an independent analyzer that it is silent on the bus.
2. Replace callback-as-packet handling with bounded, newline-delimited SPP framing and strict rejection of binary, oversized, or malformed frames.
3. Centralize SPP output in a single completion/congestion-driven writer and make all connection state handle-specific.
4. Change acknowledgment semantics so log-only stubs cannot report vehicle execution; define request IDs, replay behavior, schema, and protocol version with the Android client.
5. Resolve the recorded 4 MB versus configured 2 MB flash discrepancy and make the intended build configuration reproducible.
6. Add explicit module configuration behavior, bus-off monitoring/recovery, and clear runtime readiness state.
7. Add host/Unity tests for parser, framing, connection lifecycle, queue behavior, and CAN non-transmission safeguards before introducing any Honda CAN mappings.
8. Update stale architecture documentation and remove or clarify unused TCP-era declarations and naming.

## Build and Verification Results

- **Fresh build:** Not run. A build would rewrite existing generated files under `build/`, which would violate the read-only constraint on existing files.
- **Existing artifacts:** `build/SingleCAN-WROOM32.elf`, `build/SingleCAN-WROOM32.bin`, and a partition-table binary are present. The ELF and binary timestamps are October 7, 2026 at 16:48; these artifacts do not by themselves prove the current source was freshly rebuilt.
- **Build metadata:** Existing `build/project_description.json` identifies ESP-IDF v5.3.1 and target `esp32`. Saved compiler output reports `TAG_SINGLECAN` as unused.
- **Flash verification evidence:** Existing boot output detects 4096 kB while the image header says 2048 kB; generated flashing arguments also specify `2MB`.
- **Tests:** No repository tests were found or run.
- **Git/source integrity:** The repository was clean before the review. No existing file was modified. The only created file is this requested report.

## Final Verdict

**NOT READY**
