# SingleCAN-WROOM32 Second Copilot Review

## Executive Summary

The current backend has materially improved since the prior review: SPP input is bounded and newline-delimited, command stubs do not claim success, all `esp_spp_write()` calls go through one completion-driven writer, module configuration is session-gated and single-CAN-only, and TWAI bus-off monitoring/recovery is present. The active local and generated build configurations now specify the board's 4 MB flash.

It is **not ready for verified CAN-mapping integration**. A second outbound queue can carry old CAN telemetry into a later Bluetooth session; protocol duplicate/replay handling and peer authorization are absent; startup can be reported ready after asynchronous server startup or task failures; and transmit safeguards, recovery failure handling, tests, and reproducible build configuration need work. The existing CAN receive path also forwards raw bus frames over SPP, although PCAN-Explorer is the designated capture/decode tool.

**Current findings: 0 Critical, 1 High, 9 Medium, 3 Low.** No current command stub calls the CAN transmit API, no guessed Honda CAN mapping was found, and the specified safety-critical command families are absent.

## Previous Findings Verification

| Previous finding | Status | Current evidence |
|---|---|---|
| SPP callback boundaries were treated as complete commands | **FIXED** | `bt_spp_process_received_bytes()` accumulates bytes, splits at newline, supports coalesced frames, and discards invalid/oversized frames through the next newline. |
| Stub commands returned `status=ok` | **FIXED** | `dispatch_command()` returns `COMMAND_RESULT_NOT_IMPLEMENTED`; the response is `unsupported` with `reason=not_implemented`. |
| Multiple SPP write producers bypassed serialization | **FIXED** | `esp_spp_write()` is called only in the SPP writer task; the telemetry task uses `bt_spp_send()`. |
| Flash was configured for 2 MB instead of the board's 4 MB | **FIXED** | `sdkconfig.defaults`, current `sdkconfig`, generated config, and `build/flasher_args.json` specify 4 MB. `sdkconfig.old` is stale at 2 MB but is ignored and is not the active configuration. |
| JSON IDs and field types were not strict | **PARTIALLY FIXED** | IDs must now be finite, nonnegative integers in `int` range; required fields and optional `value` types are checked and trailing JSON is rejected. Duplicate IDs/keys, unknown fields, and replay remain unhandled. |
| SPP connection events were not handle/session-specific | **PARTIALLY FIXED** | Close, data, write, and congestion events are checked against the current handle and writer-queue items carry a session ID. State is still shared without synchronization, a new open replaces the current handle, and the separate CAN telemetry queue is not session-tagged/reset. |
| TWAI bus-off monitoring and recovery were absent | **PARTIALLY FIXED** | Alerts, `twai_initiate_recovery()`, and restart are implemented. Failed recovery initiation/restart does not have a dependable retry or terminal-health path. |
| `config.module` accepted Dual-CAN without implementing it | **FIXED** | Only `single` enables configuration; `dual` is rejected with `dual_module_not_supported`. |
| Commands could be accepted before module configuration | **FIXED** | Vehicle commands return `module_not_configured` until the current session configures Single-CAN. |
| Bluetooth readiness was reported before server startup completed | **UNRESOLVED** | `bt_spp_init()` returns after requesting asynchronous SPP initialization; `app_main()` then reports ready without waiting for successful `ESP_SPP_START_EVT`. |
| Bluetooth trust relied only on link authentication | **UNRESOLVED** | SPP requests link authentication, but there is no application peer allowlist/authorization policy. |
| Telemetry framing was inconsistent with the command protocol | **PARTIALLY FIXED** | The single writer appends newline framing, but CAN telemetry remains plain `CAN_RX ...` text rather than a JSON protocol record, and failed writes/drops have no reliable delivery or loss accounting. |
| Raw command and response contents were logged | **PARTIALLY FIXED** | Full packet and serialized response logging are absent; untrusted command strings are still logged, and raw CAN payloads are forwarded over SPP. |
| TCP-era names and documentation remained | **UNRESOLVED** | `PROJECT_ANALYSIS.md`, `tasks.h`, queue symbols, and some APIs still describe or use TCP-era names. |
| No automated safety or protocol tests existed | **UNRESOLVED** | No test source, test component, or protocol fixture is present. |

## Architecture Assessment

- `app_main()` initializes NVS, LEDs, the outbound telemetry queue, TWAI, CAN tasks, and Bluetooth SPP. The product code is an SPP command backend with TWAI; it is not an Android discovery tool.
- `singlecan_commands.c` parses JSON requests, gates vehicle commands on per-session Single-CAN configuration, and maps supported comfort/body commands to non-transmitting stubs.
- `bt_spp.c` owns newline input framing and the one completion-driven SPP writer. `tcp_queue.c` adds a second queue between CAN receive and that writer.
- `singlecan_can.c` starts TWAI in normal mode at 500 kbit/s with an accept-all filter. Normal mode remains suitable for future verified transmission; it is not listen-only and can participate in bus acknowledgment.
- The repository does not contain BabyNode Automotive, so exact cross-repository command/status/reason compatibility cannot be proven. Current backend envelopes use numeric `id`, string `type`/`command`, optional string `value`, and JSON response fields `id`, `type`, `status`, `command`, and optional `reason`.
- The stale `PROJECT_ANALYSIS.md` describes Wi-Fi, HTTP, TCP-server files, and behavior not in the current CMake target. Those services are not implemented in this repository.

## Critical Findings

None identified.

## High Findings

### H1 — CAN telemetry can cross Bluetooth session boundaries

- **Severity:** High
- **Exact file and function:** `main/tcp_queue.c` — `tcp_queue_push()`, `tcp_queue_task()`; `main/bt_spp.c` — `bt_spp_send()`, `ESP_SPP_SRV_OPEN_EVT` / `ESP_SPP_CLOSE_EVT` handling
- **Problem:** CAN telemetry is first stored in `tcp_outbound_queue`, whose items have no session identifier and which is not reset on SPP connection changes. The SPP writer's own queue is reset and tagged, but a delayed item in the upstream queue is tagged with whichever session is active when `bt_spp_send()` is eventually called.
- **Concrete failure scenario:** Telemetry remains queued during congestion or a delayed sender; the first client disconnects and another connects; the queue task later submits the earlier CAN record, which the writer treats as belonging to the new session and sends to that client.
- **Recommended correction:** Make outbound ownership session-aware end-to-end: either remove the redundant staging queue or tag each item with the originating connection generation and reject it on mismatch. Ensure connect/disconnect transitions invalidate pending items atomically.
- **Required verification test:** Hold telemetry in each queue, disconnect client A, connect client B, and release the queue. Assert no A-session item reaches B; repeat with disconnect during an in-flight write and rapid reconnect.

## Medium Findings

### M1 — JSON schema, duplicate-ID, and replay policy remain incomplete

- **Severity:** Medium
- **Exact file and function:** `main/singlecan_commands.c` — `process_json_packet()`, `parse_packet_id()`, `process_module_config()`
- **Problem:** The parser validates primary field types and the numeric ID range, but does not reject duplicate JSON keys, unknown fields, or reused request IDs. There is no replay policy. The SPP byte framer silently removes every carriage return, including one inside a JSON string, which can transform malformed input into another command. Decoded `\u0000` strings may also be compared as truncated C strings.
- **Concrete failure scenario:** A retried non-idempotent request with an already-used ID executes again after real mappings are added; two parsers may interpret duplicate keys differently; or an embedded control/NUL character makes a command/value compare as a valid prefix.
- **Recommended correction:** Agree a versioned envelope and request-ID retry policy with BabyNode Automotive; reject duplicate keys, unknown fields, embedded NUL/control characters, and invalid field combinations. Preserve valid JSON whitespace instead of deleting carriage returns inside the frame.
- **Required verification test:** Exercise duplicate keys and IDs, exact retries, unknown fields, escaped NUL, escaped control characters, CRLF, CR inside strings, wrong types, numeric boundaries, and trailing data. Assert rejected packets never dispatch and responses follow the agreed schema.

### M2 — Connection ownership is not fully synchronized or single-client-enforced

- **Severity:** Medium
- **Exact file and function:** `main/bt_spp.c` — `spp_event_handler()`, `bt_spp_send()`, `bt_spp_writer_task()`
- **Problem:** Handle checks reject many stale events, but connection fields are shared between the SPP callback and tasks as `volatile` globals rather than synchronized state. Every server-open event replaces the one global handle without rejecting or closing a previous active connection. The writer's connection/session checks and `esp_spp_write()` submission are not one atomic transition.
- **Concrete failure scenario:** A second client opens while one is active, or disconnect/reconnect overlaps a writer check; the callback and writer observe different handle/session state, leaving an orphaned client or allowing a write to target a handle from a different state transition.
- **Recommended correction:** Define and enforce a single-client policy (or implement explicit per-client state), and serialize connection transitions with the writer through a single owner/event queue or an appropriate lock. Do not rely on `volatile` for cross-task synchronization.
- **Required verification test:** Inject two opens, stale close/data/write/congestion events, close-old-after-open-new, and disconnect/reconnect at each writer transition. Assert only the selected live session can receive data and stale events cannot change its state.

### M3 — Bluetooth pairing is not application authorization

- **Severity:** Medium
- **Exact file and function:** `main/bt_spp.c` — `spp_event_handler()` (`ESP_SPP_INIT_EVT`), `ESP_SPP_SRV_OPEN_EVT`, `ESP_SPP_DATA_IND_EVT`
- **Problem:** The device is made connectable/discoverable and requests SPP link authentication, but does not authorize a specific peer at the application layer. Protocol version negotiation, request freshness, and command rate limiting are also absent.
- **Concrete failure scenario:** An unintended paired/authenticated device connects and submits commands; after verified mappings are introduced, link authentication alone does not establish that this peer is authorized to control the vehicle.
- **Recommended correction:** Define the pairing and trusted-peer model with the Android client, enforce authorization before command processing, and agree protocol-version, freshness/replay, and rate-limit behavior before CAN mappings are enabled.
- **Required verification test:** Verify unpaired and unauthorized peers cannot configure or issue commands, authorized peers can, replayed requests follow policy, and request floods do not starve connection management.

### M4 — Startup can claim readiness after asynchronous or task-start failures

- **Severity:** Medium
- **Exact file and function:** `main/main.c` — `app_main()`; `main/bt_spp.c` — `bt_spp_init()`, `spp_event_handler()` (`ESP_SPP_START_EVT`); `main/tcp_queue.c` — `tcp_queue_init()`; `main/tasks.c` — `start_can_rx_task()`
- **Problem:** `app_main()` logs ready immediately after `bt_spp_init()` accepts asynchronous initialization. The SPP start event does not publish success/failure state. Queue and CAN-task initialization APIs return `void`, so their failures do not prevent the application from reporting readiness.
- **Concrete failure scenario:** Server startup fails asynchronously or queue/health/RX task creation fails; the device still indicates ready although its command or telemetry/recovery path is unavailable.
- **Recommended correction:** Track subsystem readiness and asynchronous SPP startup status; propagate queue and task creation errors to startup policy; publish ready only when required subsystems have actually started.
- **Required verification test:** Inject queue/task allocation failures and successful, failed, delayed, and missing SPP start events. Assert readiness is withheld or explicitly degraded and never reports unavailable services as ready.

### M5 — TWAI recovery failure can leave reception paused indefinitely

- **Severity:** Medium
- **Exact file and function:** `main/tasks.c` — `initiate_bus_recovery()`, `complete_bus_recovery()`, `can_health_task()`, `can_rx_forward_task()`
- **Problem:** If `twai_initiate_recovery()` fails, the active flag is cleared but no retry is scheduled. If the post-recovery `twai_start()` fails, `s_bus_recovery_active` remains true, so the RX task keeps sleeping and never resumes. There is no surfaced terminal health state.
- **Concrete failure scenario:** A transient driver error occurs during recovery initiation/restart; no further recovery-completed event arrives, and receive remains stopped while the firmware continues running.
- **Recommended correction:** Add bounded retry/reinitialization or an explicit fault state with operator-visible failure; ensure RX cannot remain indefinitely paused without a reported health failure.
- **Required verification test:** Simulate recovery-init failure, recovery success followed by restart failure, repeated bus-off, and persistent faults. Verify retries/fault reporting and that RX resumes only after successful TWAI restart.

### M6 — Raw CAN transmit API has no verified-frame policy

- **Severity:** Medium
- **Exact file and function:** `main/singlecan_can.c` — `singlecan_send()`, `singlecan_init()`; `main/singlecan_can.h` — `singlecan_send()`
- **Problem:** `singlecan_send()` validates payload length and null data but accepts an arbitrary `uint32_t` identifier and payload, with `g_can_enabled` initially true. No allowlist or verified-message gate exists. There is no current command path to this function, but the API is available to future callers.
- **Concrete failure scenario:** A future handler or accidental caller passes a guessed, malformed, or prohibited frame; TWAI is in normal mode and can transmit it onto the bus.
- **Recommended correction:** Before adding any mapping, constrain the API to validated standard identifiers and approved frame definitions, keep it unreachable from generic client input, and require explicit verified mapping policy. Preserve normal mode for the eventual verified-transmission role.
- **Required verification test:** Use a fake TWAI driver to prove invalid IDs, unknown frames, prohibited command families, and malformed payloads never reach `twai_transmit()`; confirm only reviewed mappings can transmit.

### M7 — Raw receive telemetry conflicts with the assigned capture architecture and protocol

- **Severity:** Medium
- **Exact file and function:** `main/singlecan_can.c` — `singlecan_receive()`; `main/tasks.c` — `can_rx_forward_task()`; `main/tcp_queue.c` — `tcp_queue_task()`
- **Problem:** The firmware drains and forwards raw CAN records as plain `CAN_RX ...` strings over SPP. The writer adds a newline, but the record is not a JSON telemetry envelope consistent with the command/response protocol. The queue drops records on disconnect, queue saturation, or asynchronous write failure without durable loss accounting. This makes SingleCAN function as a bus capture/forwarding tool instead of leaving capture/decode to PCAN-Explorer 7.
- **Concrete failure scenario:** A client expecting newline-delimited JSON cannot parse a CAN record; congestion or disconnect silently loses records; raw bus data is also exposed to the connected peer.
- **Recommended correction:** Keep PCAN-Explorer 7 as the capture/decode tool. Remove raw capture forwarding from the command path or make diagnostic telemetry explicitly opt-in and schema-defined; surface queue/write loss distinctly. Do not convert the firmware into permanent capture-only/listen-only operation.
- **Required verification test:** Feed frames with varying DLC and verify the agreed protocol record shape, session behavior, and explicit drop accounting under disconnected, full-queue, congested, and failed-write conditions.

### M8 — Clean builds are not fully reproducible from tracked project configuration

- **Severity:** Medium
- **Exact file and function:** `sdkconfig.defaults`; `CMakeLists.txt` — ESP-IDF project setup; `main/CMakeLists.txt`
- **Problem:** `sdkconfig` is ignored and not tracked; the tracked defaults specify only 4 MB flash. Required Bluetooth/SPP and partition choices are present in the local ignored configuration, and the build depends on external `IDF_PATH` without a repository-enforced ESP-IDF version. Existing generated metadata says ESP-IDF 5.3.1 and `APP_REPRODUCIBLE_BUILD=false`, but does not make a clean checkout reproduce that build.
- **Concrete failure scenario:** A clean checkout generates a different SDK configuration or uses an incompatible IDF version, disabling required SPP options or changing partition/build behavior while appearing to build the same project.
- **Recommended correction:** Record the supported ESP-IDF version and required reproducible SDK/partition defaults in tracked project configuration; keep the 4 MB setting and verify the selected partition layout fits the board and image.
- **Required verification test:** Build from a clean checkout with the documented IDF version and no preexisting `sdkconfig`; compare target, required BT/SPP settings, flash size, partition table, and build result with the expected configuration.

### M9 — No automated tests protect protocol, session, queue, or CAN-safety invariants

- **Severity:** Medium
- **Exact file and function:** Repository-wide — no test sources or test component; relevant seams include `main/bt_spp.c`, `main/singlecan_commands.c`, `main/tcp_queue.c`, and `main/singlecan_can.c`
- **Problem:** No automated parser/framer, SPP lifecycle, queue/session, recovery, or CAN-safety tests are present.
- **Concrete failure scenario:** Regressions in delimiter handling, stale-session delivery, replay rejection, bus recovery, or future mapping safety can pass compile-only checks.
- **Recommended correction:** Add host-testable protocol/framing seams and ESP-IDF Unity or equivalent tests; make the non-transmission and session-isolation rules explicit test invariants before mapping work.
- **Required verification test:** Automate fragmented/coalesced/oversized/binary input; schema and replay cases; stale handles and reconnects; congestion and queue failures; TWAI recovery failures; and assertions that stubs/prohibited/unknown commands never call `twai_transmit()`.

## Low Findings

### L1 — Documentation and declarations retain TCP-era architecture

- **Severity:** Low
- **Exact file and function:** `PROJECT_ANALYSIS.md`; `main/tasks.h` — `start_tcp_server_task()`; `main/tcp_queue.h` / `main/tcp_queue.c` — `tcp_queue_*`; `main/singlecan_can.h` — `TAG_SINGLECAN`
- **Problem:** The analysis document describes nonexistent Wi-Fi/HTTP/TCP-server files; `tasks.h` declares an unimplemented TCP server task; the Bluetooth queue retains TCP names; several CAN API declarations have no current callers; and `TAG_SINGLECAN` is a static header variable.
- **Concrete failure scenario:** A maintainer follows obsolete documentation or declarations and assumes a TCP server or API exists; stale names obscure the actual SPP transport.
- **Recommended correction:** Update architecture documentation, remove obsolete declarations and unused APIs when appropriate, and use consistent SPP queue naming.
- **Required verification test:** Compare documentation/declarations with the CMake source list and symbol references; verify the documented runtime paths exist.

### L2 — CAN telemetry formats bytes beyond the reported DLC

- **Severity:** Low
- **Exact file and function:** `main/singlecan_can.c` — `singlecan_receive()`
- **Problem:** The `CAN_RX` formatter always prints eight data bytes, even when `data_length_code` is smaller. This is within the fixed array but represents bytes not present in the received frame.
- **Concrete failure scenario:** A short frame is forwarded with trailing bytes that a client mistakes for valid payload.
- **Recommended correction:** Validate DLC and format only the reported payload bytes if diagnostic telemetry remains.
- **Required verification test:** Check DLC 0, 1, 7, and 8 and assert only the corresponding number of data bytes is emitted.

### L3 — Untrusted command text remains in info-level logs

- **Severity:** Low
- **Exact file and function:** `main/singlecan_commands.c` — `process_json_packet()`, `dispatch_command()`
- **Problem:** Client-supplied command strings are logged directly. Full packets, `value`, and serialized responses are no longer logged, but escaped control characters can still produce confusing log output and repeated commands can create log volume.
- **Concrete failure scenario:** An authenticated client sends a long command containing escaped newlines repeatedly, forging/multiplying console log lines.
- **Recommended correction:** Log bounded validation outcomes or allowlisted command identifiers rather than arbitrary command strings; rate-limit repetitive diagnostics.
- **Required verification test:** Send maximum-length unknown commands with escaped control characters at high rate and verify logs remain bounded and do not contain injected lines.

## Bluetooth SPP Assessment

- Newline framing is bounded at 4096 bytes, handles fragmented and coalesced input, rejects non-ASCII/control bytes, and discards oversized/invalid frames until newline. Carriage returns are currently removed anywhere in a frame, not only treated as JSON whitespace.
- A single task owns `esp_spp_write()` and waits on completion/congestion state. Queue acceptance is not delivery confirmation; asynchronous failure drops the item.
- Stale close/data/write/congestion handles are checked, but state synchronization, multiple-open policy, full session isolation, and startup readiness remain unresolved (H1, M2, M4).
- SPP link authentication is configured, but there is no application peer authorization or request rate policy (M3).

## JSON and Protocol Assessment

- Request ID validation now rejects missing, nonnumeric, fractional, negative, non-finite, and out-of-range IDs. Required `type` and `command` fields and optional `value` type are checked; parser trailing content is rejected.
- Extra fields and duplicate keys/IDs are not rejected; replay is not deduplicated; escaped NUL/control handling can create string-prefix/logging ambiguity (M1, L3).
- `config.module=single` succeeds, `dual` is rejected, configuration resets on open/close, and commands before configuration are rejected.
- Recognized stubs return exactly `status=unsupported`, `reason=not_implemented`; unknown commands return `unsupported:unknown_command`. Configuration `ok` means the session gate was set, not that a CAN action occurred.
- Backend field shapes appear compatible with the stated BabyNode Automotive protocol, but exact compatibility—including all command aliases, status/reason expectations, telemetry, and retry behavior—cannot be established without the client repository or fixtures.

## FreeRTOS and Queue Assessment

- Queue and task allocation results are checked locally, and failures are logged. However, initialization APIs do not propagate those failures to `app_main()`, so readiness may be false (M4).
- The 64-item CAN telemetry queue and 32-item SPP writer queue are bounded and use nonblocking producer sends. Saturation/write failure drops records; no durable loss counter or delivery result reaches the caller (M7).
- Two outbound queues complicate session ownership and permit the H1 cross-session path. The task-state fields shared with the SPP callback use `volatile`, not synchronization (M2).
- CAN RX/health and writer tasks use fixed 4096-unit stacks; no stack high-water or runtime resource validation was found.

## TWAI and CAN Safety Assessment

- TWAI uses normal mode, GPIO 5 TX/GPIO 4 RX, 500 kbit/s, and an accept-all filter. This supports the intended future verified-transmission role, but is not passive/listen-only.
- `singlecan_send()` validates DLC and non-null payload but has no reviewed-frame allowlist or explicit standard-ID policy (M6). Search found no current caller from a command stub; all current recognized handlers are non-transmitting.
- The receive task forwards raw bus frames. PCAN-Explorer 7 should remain the capture/decode tool; SingleCAN should focus on validating the canonical command protocol and later transmitting only verified mappings (M7).
- Bus-off alerts and recovery are implemented, but recovery failure paths can leave the RX task permanently paused (M5).
- No brake, steering, airbag, engine-torque, transmission-operation, traction-control, eco/powertrain-mode, or cruise-control command mapping was found. No Honda identifier/payload mapping or guessed placeholder frame was found.

## Security Assessment

- SPP authentication is not an application-level authorization policy. There is no peer allowlist, protocol version negotiation, replay policy, or command rate limit (M1, M3).
- Input and queue lengths are bounded, and malformed/oversized input does not get parsed as a truncated command. The remaining protocol ambiguity and log concerns are M1 and L3.
- There is no current remote raw-CAN frame command or stub-to-transmit call path. The internal raw transmit API is still broad and must be gated before any mapping is added (M6).
- Raw CAN data is forwarded over SPP, increasing exposure to a connected client and conflicting with the assigned PCAN capture role (M7).

## Dead or Legacy Code

- `PROJECT_ANALYSIS.md` describes absent Wi-Fi, HTTP, and TCP server modules.
- `main/tasks.h` still declares `start_tcp_server_task()` without an implementation.
- `tcp_queue.*` and `TCP_QUEUE_*` refer to an SPP queue.
- `singlecan_enable_can()`, `singlecan_disable_can()`, `singlecan_get_status()`, and `singlecan_stop()` have no current caller.
- `TAG_SINGLECAN` is declared in a header and is unused in the reviewed source.

## Missing Tests

No test files or test target were found. In addition to M9, unverified cases include SPP callback ordering, session queue isolation, exact BabyNode Automotive fixtures, allocation/task-creation failure, async write failure, queue drops, and TWAI recovery failure. Existing build artifacts do not substitute for these tests.

## Readiness Checklist

| Requirement | Assessment |
|---|---|
| Bounded newline-delimited SPP input | **Pass** |
| Fragmented/coalesced input and oversize/binary rejection | **Pass**, with CR normalization caveat (M1) |
| Single completion-driven owner for `esp_spp_write()` | **Pass** |
| Old-session messages excluded after reconnect | **Fail** — H1 |
| Strict JSON fields and duplicate/replay policy | **Partial** — M1 |
| Only `config.module=single` accepted; config reset on connect/disconnect | **Pass** |
| Commands blocked before configuration | **Pass** |
| Stub response is `unsupported:not_implemented` | **Pass** |
| Application-level peer trust and rate/replay protection | **Fail** — M3 |
| SPP readiness reflects completed server startup | **Fail** — M4 |
| TWAI bus-off monitoring/recovery | **Partial** — M5 |
| TWAI appropriate for future verified transmission | **Pass**, with transmit-policy gap M6 |
| No current stub reaches CAN transmit API | **Pass** |
| Prohibited safety-critical commands absent | **Pass** |
| No guessed Honda CAN mapping | **Pass** |
| PCAN remains the capture/decode tool | **Fail** — raw RX forwarding remains (M7) |
| Reproducible clean build configuration | **Fail** — M8 |
| Automated protocol and CAN-safety tests | **Fail** — M9 |
| BabyNode Automotive compatibility | **Partial / unverified** — client source and fixtures are absent |

## Prioritized Remediation Plan

1. Prevent cross-session outbound delivery by removing or session-tagging the intermediate telemetry queue; define and enforce SPP client ownership.
2. Finalize the client protocol contract: reject duplicate/unknown fields, define request-ID replay behavior and versioning, and enforce application peer authorization and rate limits.
3. Gate raw CAN transmission behind verified frame definitions and an explicit allowlist; keep all current handlers non-transmitting and keep prohibited command families absent.
4. Make startup readiness and recovery health truthful, including asynchronous SPP startup and task/queue failures.
5. Remove raw CAN capture forwarding from the command protocol, or define explicit diagnostic telemetry with bounded loss accounting while PCAN-Explorer remains the capture/decode tool.
6. Add focused parser, SPP lifecycle, queue/session, TWAI recovery, and no-transmit tests.
7. Pin/document the ESP-IDF build environment and tracked configuration defaults; then remove stale TCP-era documentation and declarations.

## Build and Verification Results

- Inspected all 20 tracked repository files, including all C/header sources, both project documents, CMake/Kconfig, `.gitignore`, and `sdkconfig.defaults`; also inspected local ignored `sdkconfig`, `sdkconfig.old`, and generated build metadata relevant to target, partition, flash, and ESP-IDF version.
- Non-destructive commands run: `git --no-pager status --short --branch`, `git --no-pager ls-files`, `git --no-pager diff --check`, `wc -l`, targeted `rg` searches, and repository file enumeration; source/config/documentation reads were also performed.
- Existing generated metadata identifies ESP-IDF v5.3.1, target `esp32`, 4 MB flash, Classic Bluetooth/SPP enabled, and the single-app partition table. The active local `sdkconfig` and tracked flash default both specify 4 MB. `sdkconfig.old` retains obsolete 2 MB settings.
- **Build not run. Tests not run.** A build could overwrite existing files under `build/`; no tests are present. Existing generated artifacts do not prove the currently inspected source builds cleanly.
- Git was clean before report creation. No existing file, source, configuration, documentation, build output, or Git index was modified. The only file created by this review is this report.

## Final Verdict

**NOT READY**
