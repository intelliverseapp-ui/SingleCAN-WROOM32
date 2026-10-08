# SingleCAN-WROOM32 — Independent Copilot Review 4

**Review date:** 2026-10-08  
**Review type:** Independent safety, reliability, security, lifecycle, and architecture review  
**Scope:** Current repository source and tracked build configuration, with Review 3 findings checked against the current implementation.

## Executive summary

No currently exploitable security vulnerability was confirmed in the active command path. Vehicle-command handlers remain non-transmitting stubs; verified-frame lookup currently rejects all commands; and the only `twai_transmit()` call is behind the verified-frame boundary and supervised TWAI health check.

Two medium-severity release-readiness findings remain:

1. The required ESP-IDF v5.3.1 is recorded but not enforced by configuration.
2. No automated tests were found for protocol validation, SPP ownership/session lifecycle and writer failures, TWAI recovery, or the no-unverified-transmit invariant.

The SPP ownership, session isolation, bounded writer waits, framing, envelope/replay checks, rate limiting, and TWAI recovery/readiness behavior inspected for this review appear implemented as intended. These invariants have no automated regression coverage, so their status is source-inspection confirmation rather than test evidence. Review 4 did not run a clean build or test suite.

The current Android-only scope and Bluetooth Classic SPP transport are intentional and are **not** findings. BLE GATT and iPhone support are future milestones, not current requirements. No Honda Accord CAN mappings have been verified or installed.

## Findings

### R4-01 — ESP-IDF version is recorded but not enforced

- **Severity:** Medium
- **Exact file and function:** [`CMakeLists.txt`](./CMakeLists.txt), top-level CMake project configuration / ESP-IDF project include (global configuration; no C function); [`ESP-IDF_VERSION`](./ESP-IDF_VERSION)
- **Concrete problem:** `ESP-IDF_VERSION` records v5.3.1, but the top-level project loads ESP-IDF through the configured `IDF_PATH` without rejecting a different installed version. The version marker is informational rather than an enforced build constraint. Tracked `sdkconfig.defaults` now specifies the ESP32 target and the Classic-SPP-related configuration, but it does not pin the toolchain.
- **Realistic failure scenario:** A developer or build agent uses a different ESP-IDF release and obtains a successful build with potentially different component or configuration behavior, while assuming it matches the documented v5.3.1 environment.
- **Recommended correction:** Enforce the required ESP-IDF version during configuration and run builds in a pinned environment (such as a versioned CI image/container or equivalent reproducible toolchain setup).
- **Required verification test:** Configure and build with ESP-IDF v5.3.1 successfully; configure with a different version and assert that configuration fails with a clear version-mismatch diagnostic.

### R4-02 — No automated tests cover protocol, lifecycle, recovery, or no-transmit invariants

- **Severity:** Medium
- **Exact file and function:** [`main/CMakeLists.txt`](./main/CMakeLists.txt), component/test registration (no test function or test target found); repository-wide test configuration and test sources were not found.
- **Concrete problem:** There are no automated test sources, protocol fixtures, or test targets for the strict request/replay policy, SPP session ownership and isolation, writer failure/reconnect behavior, TWAI startup/recovery, or the rejection of unmapped CAN transmissions. This is an assurance gap, not evidence that these source-inspected invariants are currently violated.
- **Realistic failure scenario:** A future parser, reconnect, recovery, or mapping change compiles but regresses behavior—for example, stale queued work reaches a new session, recovery hangs or restarts indefinitely, or an unmapped command reaches the transmit boundary.
- **Recommended correction:** Add automated tests for request parsing and ordering; SPP fragmented/coalesced/malformed frames; one-client ownership and reconnect isolation; queue saturation and immediate/asynchronous/timeout write failures; TWAI startup and bounded recovery; and a mocked assertion that unmapped commands cannot call the transmit boundary. Run these tests in CI. Add protocol fixtures when authoritative vehicle mappings exist.
- **Required verification test:** Run the focused suite in CI and demonstrate coverage of malformed/duplicate/out-of-order requests, fragmented and coalesced input, disconnect during an in-flight write, completion timeout and stale completion, recovery-completion timeout/retry exhaustion, and unmapped send attempts.

## Explicit invariant verification

Results below are based on current-source inspection; no clean build or automated tests were run in this review.

| Requirement | Result | Evidence / qualification |
|---|---|---|
| Only one active SPP client may own the command channel | **Pass (source inspection)** | `main/bt_spp.c` accepts an owner and the session module synchronizes ownership. |
| Additional clients are rejected without replacing the active owner | **Pass (source inspection)** | `main/bt_spp.c` rejects an additional open while the existing owner remains active. |
| No response or queued item may cross into a later Bluetooth session | **Pass (source inspection; test gap)** | Session reset/notifications are handled in `main/bt_spp.c`; queue items in `main/bt_spp_writer.c` carry session identity. |
| No response-delivery failure may cause an indefinite writer stall | **Pass (source inspection; test gap)** | The writer's completion wait is bounded (five-second timeout); disconnect/failure handling wakes or cancels pending work. A delivery failure can still mean a response is lost. |
| SPP write submission is completion-driven and has a single call site | **Pass (source inspection)** | The writer task is the sole `esp_spp_write()` caller and waits for a matching completion. |
| Congestion, queue saturation, immediate failure, asynchronous failure, timeout, and response loss are handled | **Pass for bounded failure handling; delivery is not guaranteed** | Failures are surfaced and the affected session is disconnected; no indefinite writer wait was identified. The request/response path does not guarantee delivery or define a complete client retry/commit contract. See Review 3 finding M1 disposition. |
| SPP framing handles fragmentation, coalescing, oversized input, binary/control bytes, CRLF, and malformed JSON safely | **Pass (source inspection)** | `main/bt_spp_framer.c` provides bounded framing; CR is accepted only in CRLF framing and malformed/control input is rejected. JSON parsing/validation is in `main/singlecan_commands.c`. |
| Request envelope validation and duplicate-field rejection | **Pass (source inspection)** | `main/singlecan_commands.c` validates the envelope and rejects duplicate fields before dispatch. |
| Request-ID replay and ordering protection | **Pass (source inspection)** | Request IDs are validated before configuration or command dispatch in `main/singlecan_commands.c`. |
| Per-session request-rate limiting | **Pass (source inspection)** | Implemented in `main/singlecan_commands.c`. |
| Module configuration and session reset behavior | **Pass (source inspection)** | Session state is reset on connection lifecycle transitions; request processing applies validated configuration before dispatch. Regression tests are missing. |
| Extracted dispatcher, handlers, and response modules | **Pass (structure inspected)** | Dispatcher, command handlers, and response generation are separated into `main/singlecan_commands.c`, `main/singlecan_command_handlers.c`, and related command/response modules. |
| Command allowlist aliases and unsupported-command behavior | **Pass (source inspection)** | The dispatcher uses an explicit supported-command allowlist/aliases and rejects unsupported commands. |
| No arbitrary CAN identifier/payload acceptance from Bluetooth or runtime callers | **Pass (source inspection)** | The command path does not provide arbitrary identifier/payload submission. `main/singlecan_can.c` uses verified-frame lookup; no current mapping is installed and lookup rejects all current command values. |
| `twai_transmit()` exists at only one private verified boundary | **Pass (source inspection)** | The sole call is in `main/singlecan_can.c` behind verified-frame validation. No external caller can provide an arbitrary frame through this boundary. |
| Only RUNNING TWAI health may submit a verified frame | **Pass for current implementation** | `main/singlecan_can.c` checks supervised RUNNING health before submission. There is no installed mapping to exercise this path. |
| No command currently transmits a CAN frame | **Pass** | `main/singlecan_command_handlers.c` handlers remain stubs; verified lookup currently rejects all commands. |
| Runtime TWAI health gates transmission | **Pass for current implementation** | The verified send path checks enabled CAN and supervised RUNNING state before its sole transmit call. |
| TWAI startup handshake and final readiness reporting | **Pass (source inspection)** | `start_can_rx_task()` returns status and `app_main()` checks CAN-task startup and SPP-server readiness before setting the ready indication. |
| Bus-off detection, bounded recovery, recovery timeout, restart retries, terminal fault behavior | **Pass (source inspection; test gap)** | `main/tasks.c` supervises bus health, bounds recovery completion, retries restart, and enters terminal fault behavior on failure/deadline. |
| Received-CAN privacy; no raw payload forwarding or logging | **Pass (source inspection)** | The receive path drains frames without exposing their contents in responses or logs. |
| Clean tracked defaults build the ESP32 Classic-SPP profile without unintentionally enabling BLE/GATT | **Configuration intent present; clean-build result unverified** | `sdkconfig.defaults` tracks the ESP32 target and Classic-SPP-related settings. No clean build was run, so the resulting profile was not verified in this review. BLE/GATT and iPhone support are intentionally postponed and are not defects. |
| ESP-IDF v5.3.1 requirement | **Partial** | The required version is recorded in `ESP-IDF_VERSION` but not enforced; see R4-01. |
| Current Android-only product scope | **Pass / not a finding** | Android-only and Bluetooth Classic SPP are intentional for version 1.0. No iPhone/BLE omission is reported as a defect. |

## Previous Review 3 findings — verification table

Disposition is based on checking each reported concern against the current source, not on copying the earlier conclusion.

| Previous finding | Review 4 disposition |
|---|---|
| **H1 — SPP ownership and writer could be stranded by replacement/stale completion** | **Fixed in primary behavior.** Additional clients are rejected without replacing the owner; session state is synchronized; disconnect wakes/cancels writer work; completion waits are bounded. Stale-completion/reconnect behavior still needs automated lifecycle tests (R4-02). |
| **M1 — SPP congestion/write failures could lose responses** | **Partially fixed.** Write failures are surfaced and the affected session is disconnected; bounded waits prevent an indefinite writer stall. Response delivery is not guaranteed and the request processor has no delivery/commit contract. Clarify client retry/idempotency behavior before adding mappings. |
| **M2 — No application peer authorization, protocol version, or rate limit** | **Partially fixed.** Per-session rate limiting is present. Application-level peer allowlisting/authorization and protocol-version negotiation are not established. This is a pre-mapping security gate, not a current vehicle-control exploit while handlers remain stubs. |
| **M3 — CR was removed from anywhere in an input frame** | **Fixed.** Framing accepts CR only as part of CRLF and rejects malformed control/framing input. |
| **M4 — CAN task startup failures might not prevent ready indication** | **Fixed.** CAN task startup returns status and `app_main()` checks it before readiness reporting. |
| **M5 — Recovery could wait indefinitely for completion** | **Fixed.** Recovery completion has a deadline and failure enters a terminal fault state. |
| **M6 — Verified transmission did not check runtime TWAI health** | **Fixed.** The verified send path requires supervised RUNNING state before submission. No mapping is currently installed. |
| **M7 — Clean-build configuration incomplete/unpinned** | **Partially fixed.** Tracked defaults specify target and relevant Classic-SPP/hardware settings, and the ESP-IDF version is recorded. Version enforcement remains unresolved (R4-01); a clean build was not performed. |
| **M8 — No automated protocol/lifecycle/CAN-safety tests** | **Unresolved (R4-02).** No test sources, fixtures, or test target were found. |
| **L1 — Stale analysis document and unused APIs** | **Unresolved documentation drift.** `PROJECT_ANALYSIS.md` still describes removed Wi-Fi/HTTP/TCP behavior and obsolete control paths. This is not counted as a security finding. Update or replace it so it reflects the current Android-only, Classic-SPP, non-transmitting phase. |

## Confirmed fixed, unresolved, and newly discovered

- **Confirmed fixed in current primary behavior:** Review 3 H1's ownership/replacement issue; M3's CR framing issue; M4's startup readiness issue; M5's unbounded recovery wait; and M6's missing TWAI health gate.
- **Still unresolved or partially resolved:** M1 response-loss/retry semantics; M2 application peer authorization and protocol versioning; M7 enforceable ESP-IDF version pinning (R4-01); M8 automated regression tests (R4-02); and L1 stale analysis documentation.
- **Newly discovered exploitable vulnerabilities:** None confirmed.
- **New Review 4 findings:** R4-01 and R4-02 formalize current release-readiness gaps. They correspond to unresolved/partially resolved Review 3 concerns rather than new CAN-control vulnerabilities.

## Severity counts

| Severity | Count |
|---|---:|
| Critical | 0 |
| High | 0 |
| Medium | 2 |
| Low | 0 |
| **Total** | **2** |

## Prioritized next actions

1. **Add focused automated safety and lifecycle tests (R4-02).** Gate integration on cases proving session isolation, bounded writer failure handling, strict parser/replay/rate behavior, bounded TWAI recovery, and no transmission for unmapped commands.
2. **Enforce and reproduce the ESP-IDF v5.3.1 build (R4-01).** Pin the toolchain, reject mismatches, and run a clean build from tracked defaults. Verify the selected target and Classic-SPP profile and that BLE/GATT is not unintentionally enabled.
3. **Define application peer authorization and protocol-version negotiation before installing any mappings.** Keep the current Android-only/Spp transport scope; do not treat future BLE/iPhone work as a release defect.
4. **Define response-loss, retry, and idempotency semantics before state-changing commands exist.** Current failure handling is bounded, but a disconnect can lose a response and the request path does not provide a delivery/commit contract.
5. **Refresh `PROJECT_ANALYSIS.md` and remove or document obsolete APIs only after confirming their actual call sites.** Keep architecture documentation aligned with the current modules and non-transmitting phase.
6. **Keep all vehicle handlers non-transmitting until mappings are verified and reviewed.** Any future mapping must remain behind the single private verified boundary and RUNNING TWAI health gate.

## Release-readiness assessment

**Not ready for verified vehicle-control mapping integration.** The current command handlers do not transmit and no mapping is installed, and the inspected path has meaningful safeguards: explicit command validation, bounded session-aware SPP writing, a single verified transmit boundary, runtime TWAI health gating, and bounded recovery. However, the toolchain version is not enforced, the clean Classic-SPP build profile was not verified in this review, critical safety/lifecycle behavior has no automated regression suite, and peer authorization plus response-loss/retry semantics remain to be resolved before mappings are introduced.

**Current non-transmitting Android-only phase:** No missing iPhone support or BLE/GATT support is a defect. This review performed source inspection only; no build or tests were run.
