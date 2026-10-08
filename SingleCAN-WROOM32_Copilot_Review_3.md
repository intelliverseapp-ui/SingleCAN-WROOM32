# SingleCAN-WROOM32 Third Copilot Review

## Executive Summary

The current implementation has closed several material findings from review 2: the TCP-era CAN telemetry queue is removed, received CAN payloads are no longer logged or forwarded, JSON fields and monotonically increasing per-session request IDs are validated, and SPP readiness waits for the server-start event. All current vehicle handlers remain non-transmitting stubs, the verified-mapping lookup contains no Honda frame, and no prohibited safety-critical command family is present.

The project is **not ready for verified CAN-mapping integration**. SPP connection ownership is not single-client-enforced, and a stale in-flight write completion can leave the only writer blocked indefinitely. There are also gaps in readiness propagation, bus-off recovery deadlines, CAN-health gating, input normalization, peer authorization/rate limiting, response delivery under congestion, reproducible clean builds, and automated verification.

**Current findings: 0 Critical, 1 High, 8 Medium, 1 Low.** Exact BabyNode Automotive compatibility is plausible from the implemented envelope, but cannot be proven without the client implementation or protocol fixtures.

## Second-Review Findings Verification

| Second-review finding | Status | Current verification |
|---|---|---|
| CAN telemetry could cross Bluetooth sessions | **FIXED** | CAN receive frames are drained but not logged, queued, serialized, or sent over SPP. The separate TCP-era telemetry queue is absent. |
| JSON schema, duplicate-key, duplicate-ID, and replay policy were incomplete | **FIXED** | Unknown and duplicate fields are rejected; ID fields must be unique nonnegative integers; IDs must strictly increase within a session and are rejected before configuration or command dispatch. |
| SPP connection ownership was not fully synchronized or single-client-enforced | **PARTIALLY FIXED** | Handles are checked on data/close/write events, queued items carry a session ID, and session state resets on open/close. A second open can replace an active handle, shared state is not protected by synchronization, and a stale in-flight completion can strand the writer (H1). |
| Bluetooth pairing was not application-level authorization | **UNRESOLVED** | SPP requests link authentication, but there is no application peer allowlist/authorization, protocol version negotiation, or rate limit (M2). |
| Startup could claim readiness before asynchronous SPP startup completed | **FIXED** | `app_main()` waits for successful `ESP_SPP_START_EVT` confirmation and checks server readiness before setting the ready indication. CAN task startup failures can still be followed by the ready indication (M4). |
| TWAI recovery failure could leave reception paused indefinitely | **PARTIALLY FIXED** | Recovery initiation and driver restart have bounded retries and terminal fault handling. Waiting for `TWAI_ALERT_BUS_RECOVERED` has no deadline, so a missing alert leaves the subsystem in `RECOVERING` (M5). |
| Raw CAN transmission lacked a verified-frame policy | **FIXED** | The public send API accepts only a private enum, and the lookup currently returns `ESP_ERR_NOT_SUPPORTED` for every value. No arbitrary CAN ID/payload API exists and no mapping is installed. |
| Raw receive telemetry conflicted with the assigned PCAN capture architecture | **FIXED** | The receive path only drains TWAI frames; it does not output frame contents. PCAN hardware and PCAN-Explorer 7 remain the capture/decode path. |
| Clean builds were not fully reproducible | **UNRESOLVED** | `sdkconfig` is ignored and the tracked defaults specify only 4 MB flash. The ESP-IDF version and the required target/Bluetooth/partition settings are not pinned in tracked build configuration (M7). |
| Automated protocol and CAN-safety tests were absent | **UNRESOLVED** | No test source, test fixture, or test target was found (M8). |
| TCP-era documentation and declarations remained | **PARTIALLY FIXED** | `tcp_queue.c`, `tcp_queue.h`, and TCP task declarations are absent, but `PROJECT_ANALYSIS.md` still documents removed TCP/Wi-Fi/HTTP behavior (L1). |
| Untrusted command text remained in logs | **FIXED** | Current request logs contain lengths, IDs, statuses, and generic rejection messages; raw command/value/packet text is not logged. |
| Short CAN frames were previously formatted as eight-byte telemetry | **FIXED** | There is no current CAN-frame formatter or telemetry path. Received DLC is bounded to Classic CAN's maximum before the frame is discarded from the application path. |

## Architecture Assessment

- The active application is an ESP-IDF ESP32 firmware with Bluetooth Classic SPP, a newline-framed JSON command protocol, and TWAI in normal mode.
- `main/singlecan_commands.c` validates the request envelope, configures the session as Single-CAN, enforces increasing IDs, and dispatches allowlisted commands to logging-only stubs.
- `main/bt_spp.c` owns receive framing and the single completion-driven SPP writer. The SPP writer queue is bounded and each queued item records the current session ID.
- `main/singlecan_can.c` privately owns the verified-frame lookup and transmission implementation. No Honda mapping is installed; its lookup rejects all currently defined values.
- `main/tasks.c` drains received frames without exposing their contents and handles TWAI health alerts and bus-off recovery.
- The protocol shape matches the requested fields: numeric `id`, `type`, `command`, optional string `value`; responses include `id`, `type`, `status`, `command`, and optional `reason`. The repository contains no BabyNode Automotive client code or fixture, so exact command aliases, ID behavior, and status/reason expectations cannot be integration-tested here.

## Critical Findings

None identified.

## High Findings

### H1 — SPP ownership changes can strand the writer

- **Severity:** High
- **Exact file and function:** `main/bt_spp.c` — `spp_event_handler()`, `bt_spp_writer_task()`, `bt_spp_mark_connected()`, `bt_spp_mark_disconnected()`
- **Problem:** `ESP_SPP_SRV_OPEN_EVT` unconditionally replaces the active handle/session instead of rejecting or closing a second client. State shared between the callback and writer task is `volatile`, not protected as a connection-state transaction. If a write is in flight when a close/reconnect or second open occurs, the new open clears writer event bits; the old write callback is then ignored as a stale handle. The writer waits indefinitely for a write-complete/failed bit that may never arrive.
- **Concrete failure scenario:** Client A has an SPP write in progress. A disconnects and B opens before the writer consumes the failure event. B's open clears that event and changes the handle; A's late write event is ignored. The writer remains blocked and responses for B accumulate until the queue fills. A second client can also replace current ownership while the first connection remains open.
- **Recommended correction:** Enforce one active peer by rejecting/closing additional opens. Serialize connection state transitions and writer cancellation/completion using a lock or a single owner task; associate in-flight operations with a generation, wake/cancel them on every session transition, and use a bounded completion timeout with explicit fault handling.
- **Required verification test:** Simulate a second open while connected, disconnect/reconnect during an in-flight write, and a delayed stale completion. Verify the extra client is rejected, the writer resumes for the valid session, and no prior-session item is sent to a later session.

## Medium Findings

### M1 — SPP congestion and write failures can lose protocol responses

- **Severity:** Medium
- **Exact file and function:** `main/bt_spp.c` — `bt_spp_send()`, `bt_spp_writer_task()`, `spp_event_handler()`
- **Problem:** Queue-full, immediate `esp_spp_write()` failures, and asynchronous write failures are logged and the affected response is dropped; there is no delivery result back to the request processor or response retry/terminal policy. `ESP_OK` intentionally means only “queued,” not delivered.
- **Concrete failure scenario:** Under sustained congestion or a burst exceeding the 32-item queue, a command is processed but its response cannot be queued or its write fails. BabyNode Automotive sees no response and may time out or retry, while firmware logs are the only indication of the loss.
- **Recommended correction:** Define a response-delivery policy (including whether to close a failed session), expose bounded queue/drop/failure accounting, and apply protocol backpressure or rate limiting so command acceptance does not silently become response loss.
- **Required verification test:** Fill the queue, force immediate and asynchronous write failures, and hold congestion; verify response-loss accounting, caller-visible failure behavior, and recovery of subsequent responses.

### M2 — No application-level peer authorization, protocol version, or rate limit

- **Severity:** Medium
- **Exact file and function:** `main/bt_spp.c` — `bt_spp_init()`, `spp_event_handler()`; `main/singlecan_commands.c` — `process_json_packet()`
- **Problem:** The server is discoverable and requests SPP link authentication, but command dispatch has no application-level peer allowlist/authorization, protocol version negotiation, or request-rate limit. Link authentication alone does not establish that the connected peer is the intended BabyNode Automotive instance.
- **Concrete failure scenario:** A nearby peer able to establish an authenticated SPP link can occupy the control channel, send valid configuration/command envelopes, or generate enough responses to congest the writer. Current handlers are stubs, but this must be resolved before any verified mappings are added.
- **Recommended correction:** Specify trusted-peer enrollment/authorization and the protocol version contract; reject unauthorized peers before command processing and enforce a bounded per-session request rate.
- **Required verification test:** Attempt connection from an unauthorized paired device, submit unsupported protocol versions, and exceed the request limit; verify denial/throttling and no command dispatch.

### M3 — Carriage returns are removed from anywhere in an input frame

- **Severity:** Medium
- **Exact file and function:** `main/bt_spp.c` — `bt_spp_process_received_bytes()`
- **Problem:** Every `'\r'` is discarded, including a carriage return inside a JSON string. This transforms malformed JSON instead of rejecting it.
- **Concrete failure scenario:** A corrupted or crafted string such as a command token containing a raw carriage return can be normalized into a valid allowlisted command. The handlers are currently non-transmitting, but the altered input could reach a real mapping after integration.
- **Recommended correction:** Permit only the intended CRLF framing terminator (if required) and reject carriage returns elsewhere in the frame rather than deleting them.
- **Required verification test:** Confirm LF and supported CRLF frames work, while raw CR inside a JSON string or token is rejected without dispatch; also cover fragmented/coalesced framing.

### M4 — CAN task failures do not prevent the final ready indication

- **Severity:** Medium
- **Exact file and function:** `main/tasks.c` — `start_can_rx_task()`; `main/main.c` — `app_main()`
- **Problem:** `start_can_rx_task()` returns `void`. Task-creation failures and asynchronous TWAI alert-configuration failure set a fault state, but `app_main()` cannot observe those outcomes and can proceed to set the green ready indication and log readiness once SPP starts.
- **Concrete failure scenario:** The health task fails to configure alerts or the receive-drain task cannot be created. Bluetooth startup succeeds, after which `app_main()` reports ready even though TWAI health/reception supervision is faulted or absent.
- **Recommended correction:** Return and propagate task-start results, and add a task-ready/fault handshake so final readiness requires successful CAN task initialization as well as SPP server startup.
- **Required verification test:** Inject health-task and receive-task creation failures and TWAI alert-configuration failure; assert startup enters the fault state and never reports ready.

### M5 — TWAI recovery can wait forever for a completion alert

- **Severity:** Medium
- **Exact file and function:** `main/tasks.c` — `can_health_task()`, `process_twai_alerts()`, `complete_bus_recovery()`
- **Problem:** Initiation and restart retries are bounded, but `twai_read_alerts(..., portMAX_DELAY)` has no recovery deadline. If recovery is initiated successfully but `TWAI_ALERT_BUS_RECOVERED` never arrives, health remains `RECOVERING` and the receive-drain task stays paused indefinitely without entering the terminal fault state.
- **Concrete failure scenario:** The bus remains physically faulted or the recovery-complete alert is lost; the health task blocks forever, leaving the subsystem neither running nor faulted.
- **Recommended correction:** Bound the wait for recovery completion and transition to an explicit fault or a separately bounded retry policy when the deadline expires.
- **Required verification test:** Simulate successful recovery initiation with no completion alert, alert-read failure, and late completion; verify timeout, terminal state, and no indefinite pause.

### M6 — Verified transmission does not consult runtime TWAI health

- **Severity:** Medium
- **Exact file and function:** `main/singlecan_can.c` — `singlecan_send_verified()`; `main/tasks.c` — TWAI health-state helpers
- **Problem:** The send gate checks `g_can_enabled` and whether a mapping exists, but it cannot check the `RUNNING`/`RECOVERING`/`FAULTED` health state maintained privately in `tasks.c`. There is no mapping today, so current calls are rejected; once a mapping is added, the API can attempt submission during recovery or a terminal fault.
- **Concrete failure scenario:** A future command mapping is installed and a caller submits it while bus-off recovery is active. The send path relies on driver behavior rather than the application's explicit health state and has no application-level guarantee against submitting during recovery.
- **Recommended correction:** Make runtime TWAI health part of the private transmission gate and define how any pending transmit work is handled across bus-off/recovery. Keep the health check and actual submission synchronized with state transitions.
- **Required verification test:** With a test mapping, attempt sends in STARTING, RUNNING, RECOVERING, and FAULTED states; only RUNNING may reach `twai_transmit()`, and recovery must not release stale queued commands.

### M7 — Clean-build configuration is not pinned or complete

- **Severity:** Medium
- **Exact file and function:** Root `CMakeLists.txt`, `main/CMakeLists.txt`, `sdkconfig.defaults`
- **Problem:** The build imports whichever ESP-IDF is supplied through `IDF_PATH`; no required IDF version or dependency lock is recorded. The tracked `sdkconfig.defaults` contains only the 4 MB flash setting, while `sdkconfig` is ignored and currently supplies target, partition, Classic Bluetooth, and SPP settings.
- **Concrete failure scenario:** A clean checkout on a different ESP-IDF release or without the local ignored `sdkconfig` can configure different target/features/partition settings or fail to build the SPP component.
- **Recommended correction:** Document/pin a supported ESP-IDF version and encode all required clean-build target, Bluetooth/SPP, partition, and flash settings in tracked reproducible configuration.
- **Required verification test:** From a clean checkout with no `sdkconfig` or build directory, configure and build using the documented ESP-IDF version; verify target, partition table, flash size, and SPP options.

### M8 — No automated protocol, lifecycle, or CAN-safety tests

- **Severity:** Medium
- **Exact file and function:** Repository test/build configuration; `main/singlecan_commands.c`, `main/bt_spp.c`, `main/tasks.c`, `main/singlecan_can.c`
- **Problem:** No automated tests, protocol fixtures, or test target exist for parsing/replay, SPP lifecycle and queue ownership, recovery, or the no-arbitrary-transmit invariant.
- **Concrete failure scenario:** Regressions in duplicate-field rejection, session isolation, recovery timeout, or future mapping gating can compile without any test detecting them.
- **Recommended correction:** Add host/unit tests for protocol parsing and replay, mocked SPP lifecycle/queue tests, TWAI recovery-state tests, and a static/runtime assertion that no unverified path reaches `twai_transmit()`. Add fixtures matching BabyNode Automotive requests and responses.
- **Required verification test:** Run the focused suite on every build; include malformed/oversized/binary/CR frames, duplicate and out-of-order IDs, session reconnect races, queue saturation, recovery failures, and rejected unverified CAN sends.

## Low Findings

### L1 — Stale analysis document and unused public APIs remain

- **Severity:** Low
- **Exact file and function:** `PROJECT_ANALYSIS.md`; `main/singlecan_can.h` / `singlecan_can.c` — `singlecan_enable_can()`, `singlecan_disable_can()`, `singlecan_get_status()`, `singlecan_stop()`; `main/singlecan_leds.h` / `singlecan_leds.c` — unused status helpers
- **Problem:** `PROJECT_ANALYSIS.md` describes absent Wi-Fi, HTTP, TCP-server, and TCP-queue components and an obsolete raw-transmit interface. Several CAN and LED APIs have declarations/definitions but no current callers; `singlecan_get_status()` reports only the enable flag rather than live TWAI health.
- **Concrete failure scenario:** A maintainer follows the analysis document or assumes an unused status API reflects bus health and makes an incorrect integration decision.
- **Recommended correction:** Refresh the analysis to match the SPP/PCAN architecture, remove or clearly mark unused APIs, and make any retained status API report its actual scope and health limitations.
- **Required verification test:** Search the active source/documentation for removed TCP/Wi-Fi/HTTP APIs and confirm each retained public declaration has a caller or a documented supported use.

## Bluetooth SPP Assessment

- One code location calls `esp_spp_write()`, inside `bt_spp_writer_task()`.
- The writer serializes writes, waits for write completion, tracks congestion, bounds messages/queue size, tags queued items with a session ID, and logs queue/API/completion failures.
- Connection events are handle-checked, but additional opens are not rejected. Connection state is shared with the writer using `volatile` fields without a lock or atomic state transition. Stale in-flight completions can be ignored after session replacement, leaving the writer blocked (H1).
- Queue acceptance is not delivery; queue-full and write failures can lose responses (M1). No raw CAN telemetry enters this pipeline.
- Input framing handles fragmented and coalesced newline-delimited frames, enforces a 4096-byte maximum, discards binary/control input until newline, and processes only complete frames. It removes CR throughout a frame rather than strictly handling CRLF at the terminator (M3).
- `bt_spp_wait_until_ready()` waits for server-start success and `app_main()` checks it before its final ready state. CAN task readiness is not included in the same final readiness decision (M4).

## JSON and Replay Assessment

- The root must be an object containing exactly one each of `id`, `type`, and `command`; only optional `value` is accepted. Unknown fields and duplicate object fields are rejected.
- `type` must be `"command"`; `command` and `value` must be safe strings of bounded length. `config.module` accepts the semantic value `single` (case-insensitive); all other module values are rejected.
- IDs must be finite, nonnegative integral JSON numbers within `int` range. A strictly increasing high-water mark is maintained per session. Replayed and out-of-order IDs are rejected before module configuration or command dispatch.
- Trailing content is rejected. Escaped NUL is screened; decoded control characters in protocol strings are rejected by string validation. Carriage-return stripping before parsing remains a normalization gap (M3).
- Requests with the normal stated `id`, `type`, `command`, and `value` fields are structurally supported, and responses contain the stated `id`, `type`, `status`, and `reason` fields as applicable. Exact compatibility remains unverified without client fixtures.

## FreeRTOS and Synchronization Assessment

- SPP queues and task/event-group allocations are checked. The outbound queue is fixed at 32 messages and senders use nonblocking enqueue.
- SPP connection/session fields shared with the writer are `volatile`; this does not make the compound connection transition atomic or protect the in-flight write state.
- A stale completion can block the writer indefinitely (H1). Queue saturation and write failures drop responses after logging (M1).
- CAN health and receive task creation failures are logged and fault the TWAI state, but the `void` task-start API does not let `app_main()` stop the final ready sequence (M4).

## TWAI and CAN Safety Assessment

- TWAI is initialized in `TWAI_MODE_NORMAL` at 500 kbit/s with GPIO 5 TX and GPIO 4 RX. This preserves the intended future verified-transmission role; capture/decode remains assigned to PCAN.
- There is exactly one `twai_transmit()` call site, in private `transmit_verified_frame()`. No public API accepts an arbitrary CAN ID or payload.
- `singlecan_send_verified()` selects only a `singlecan_verified_command_t`; the enum currently contains only `SINGLECAN_VERIFIED_COMMAND_NONE`, and lookup rejects all values. No verified Honda mapping is installed.
- All allowlisted command handlers are logging-only stubs. No handler calls `singlecan_send_verified()` or any raw transmit API.
- No brake, steering, airbag, engine-torque, transmission-operation, traction-control, eco/powertrain-mode, or cruise-control command family is present.
- Received frames are drained and discarded by the application. No raw CAN identifier, DLC, or payload is logged, queued, serialized, or sent over Bluetooth.
- Bus-off initiation and restart each have at most three attempts and failures enter `FAULTED`. Waiting for recovery completion is not bounded (M5); transmission is not gated on the separate runtime health state once mappings are eventually added (M6).

## Security Assessment

- Input and outbound buffers are bounded; JSON types, schema, duplicate fields, IDs, and replay order are checked.
- The discoverable SPP server requests link authentication, but there is no application-level peer authorization, version negotiation, or rate limiting (M2).
- No arbitrary CAN frame ingress or installed CAN mapping exists. Current vehicle command handlers remain non-transmitting.
- Malformed carriage-return input is normalized rather than rejected, and queue pressure can cause response loss (M1, M3).

## Build Reproducibility Assessment

- Installed ESP-IDF reports **v5.3.1**. The active local ignored `sdkconfig` selects `esp32`, 4 MB flash, the single-app partition table, and Bluetooth Classic/SPP.
- Tracked `sdkconfig.defaults` specifies only 4 MB flash. It does not pin ESP-IDF or record the local target/Bluetooth/partition configuration. `sdkconfig.old` is also ignored and contains stale 2 MB settings; it is not the active configuration.
- Root CMake delegates to `$IDF_PATH`; no dependency lock or repository build-version declaration was found. Clean-build reproducibility is unresolved (M7).
- A clean build was **not run**: the existing repository contains generated files under `build/`, while the review permits creating/overwriting only the requested report and explicitly forbids modifying build files. No existing build output was changed.

## Dead or Legacy Code

- `tcp_queue.c` and `tcp_queue.h` are completely absent; no TCP queue/task declaration remains in active source.
- `PROJECT_ANALYSIS.md` still describes removed TCP/Wi-Fi/HTTP components and raw CAN telemetry/transmission behavior (L1).
- `singlecan_enable_can()`, `singlecan_disable_can()`, `singlecan_get_status()`, `singlecan_stop()`, and LED status helpers have no current callers. The status API does not report TWAI health (L1).
- No source path formats CAN frames as eight bytes; the former short-frame telemetry issue is no longer applicable.

## Missing Tests

No test source, protocol fixture, or test target was found. Missing coverage includes:

- fragmented/coalesced, oversized, binary, CRLF, and malformed JSON input;
- strict schema, duplicate fields, escaped controls, request-ID replay/order, and session reset;
- SPP open/close ordering, multiple-client rejection, stale completion, queue ownership, congestion, queue saturation, and write failures;
- task-start/readiness failure, TWAI recovery initiation/completion/restart failures, and terminal faults;
- proof that current stubs and all unmapped command IDs cannot reach `twai_transmit()`;
- BabyNode Automotive request/response fixtures for IDs, command aliases, status, and reason fields.

## Readiness Checklist

| Requirement | Assessment |
|---|---|
| `tcp_queue.c` and `tcp_queue.h` fully removed | **Pass** |
| Single `esp_spp_write()` call location | **Pass** |
| Single private `twai_transmit()` call location | **Pass** |
| No public arbitrary CAN ID/payload API | **Pass** |
| No verified Honda mapping installed | **Pass** |
| Every current command handler is non-transmitting | **Pass** |
| Prohibited Phase 1 safety-critical command families absent | **Pass** |
| No raw CAN frame logged, queued, serialized, or sent over Bluetooth | **Pass** |
| `config.module` accepts only `single` | **Pass** (case-insensitive) |
| Configuration and replay history reset on SPP session open/close | **Pass** |
| Duplicate/replayed/out-of-order IDs rejected before dispatch | **Pass** |
| Unknown and duplicate JSON fields rejected | **Pass** |
| SPP server confirmed before final ready indication | **Pass** |
| CAN task readiness included in final ready indication | **Fail** — M4 |
| TWAI retries bounded and explicit fault state | **Partial** — no recovery-completion deadline (M5) |
| Runtime TWAI health gates future verified sends | **Fail** — M6 |
| Single-client policy and writer recovery across reconnect | **Fail** — H1 |
| Application peer authorization, protocol version, rate limiting | **Fail** — M2 |
| Clean-build reproducibility | **Fail** — M7 |
| Automated protocol/lifecycle/CAN-safety tests | **Fail** — M8 |
| Normal BabyNode Automotive traffic compatibility | **Shape supported; not verified** — no client or fixture is present |

## Prioritized Remediation Plan

1. Enforce one SPP owner and make connection transitions cancel/wake the writer safely; prevent stale in-flight completions from blocking later sessions.
2. Propagate CAN task startup/health readiness to `app_main()` and put a deadline and terminal policy around recovery completion.
3. Before adding mappings, gate transmission on synchronized TWAI health and define pending-transmit behavior during bus-off/recovery.
4. Establish application peer authorization, protocol versioning, and per-session request limits; define observable behavior for response queue/write failures.
5. Reject carriage returns except at the explicitly supported frame terminator.
6. Pin and document the clean-build ESP-IDF environment and tracked target, Bluetooth/SPP, partition, and flash configuration.
7. Add automated parser, replay, SPP lifecycle/queue, recovery, no-transmit, and BabyNode Automotive compatibility tests.
8. Update stale analysis documentation and remove or clarify unused public APIs.

## Build and Verification Results

- **Existing tests:** None found; no test runner was run.
- **Build:** No clean build run, to avoid creating/overwriting existing or additional build/configuration files under the strict file-creation restriction. Installed tool/version and current configuration were inspected read-only.
- **Configuration observed:** ESP-IDF v5.3.1; local active configuration is ESP32, 4 MB flash, single-app partition, Bluetooth Classic and SPP enabled. The local `sdkconfig` is ignored; tracked defaults only set flash size.
- **Git state before report creation:** clean; `git diff --check` passed; no tracked diff was present.
- **Non-destructive shell commands used:**
  - `git status --short --branch && git --no-pager ls-files`
  - `find . -type f -not -path './.git/*' -print | sort`
  - `wc -l main/*.c main/*.h CMakeLists.txt main/CMakeLists.txt Kconfig.projbuild sdkconfig sdkconfig.defaults sdkconfig.old PROJECT_ANALYSIS.md SingleCAN-WROOM32_Copilot_Review.md SingleCAN-WROOM32_Copilot_Review_2.md`
  - `command -v idf.py; command -v cmake; command -v ninja`
  - `idf.py --version`
  - Read-only `rg` inspections of function declarations, SPP/TWAI transmit call sites, raw CAN/TCP queue references, prohibited command families, untrusted log arguments, configuration settings, and build metadata.
  - `find main -maxdepth 1 -type f -print | sort`
  - `find . -path './.git' -prune -o -path './build' -prune -o -type f \( -name 'tcp_queue.c' -o -name 'tcp_queue.h' \) -print`
  - `find . -path './.git' -prune -o -path './build' -prune -o -type f \( -iname '*test*' -o -iname '*fixture*' \) -print`
  - Read-only `rg` inspections of `sdkconfig`, `sdkconfig.defaults`, `sdkconfig.old`, and generated `build` metadata.
  - `git --no-pager diff --check && git --no-pager diff --name-only`
  - Final scope check: `git status --short --branch && git --no-pager diff --check && git --no-pager diff --name-only`
- **Files:** This report is the only file created by the review. Source, headers, configuration, documentation, generated build files, tests, and Git state were not modified.

## Final Verdict

**NOT READY**
