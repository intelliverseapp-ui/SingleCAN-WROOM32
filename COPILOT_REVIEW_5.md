# SingleCAN-WROOM32 — Copilot Review 5

**Review date:** 2026-10-09
**Branch/checkpoint:** `singlecan-spp-session-tests` / `22cfcf3`
**Scope:** Production C and headers, CMake/configuration, all test projects and sources, Git history on this branch, and prior Copilot review reports. This was a read-only review; only this report is added.

## Executive summary

The two formal Review 4 findings have been addressed in the production path: root CMake enforces ESP-IDF 5.3.1, and focused protocol, framing, SPP session/writer, TWAI supervisor, command-dispatch, and CAN-invariant test projects now exist. Source inspection confirms that the application command handlers are still non-transmitting stubs, verified mapping lookup rejects every current enum value, and `main/singlecan_can.c` contains the only `twai_transmit()` call. No current client-controlled route to an unverified CAN frame was found.

One current TWAI supervision defect remains: unexpected alert-read errors are logged but do not move health out of `RUNNING`. Other remaining concerns are pre-mapping gates or assurance gaps, not evidence of current unauthorized CAN transmission. The reported 72 passing hardware tests are valuable for the cases exercised, but do not establish coverage of every failure edge listed below.

## Findings

### 1. Unexpected TWAI alert-read failures leave health reported as RUNNING

- **Severity:** Medium
- **Exact reference:** [`main/tasks.c`](./main/tasks.c), `can_health_task()`, lines 628–649; health query at line 126.
- **Evidence:** `ESP_ERR_TIMEOUT` is treated as normal polling. For other read errors, the task logs and lights the error indicator, then loops without changing `s_twai_health`; `singlecan_tasks_is_twai_running()` can continue returning true. Only `ESP_ERR_INVALID_STATE` outside recovery/fault enters the terminal fault state.
- **Practical impact:** If alert monitoring repeatedly fails for an unexpected reason, the subsystem can claim `RUNNING` while bus-off and other health transitions are no longer being observed. The current empty mapping table prevents transmission now, but a future mapping could rely on a stale health result.
- **Recommended correction:** Define bounded handling for unexpected alert-read errors and transition to a non-transmitting fault state if monitoring cannot be restored. Add a test proving `RUNNING` is cleared on persistent read failure.
- **Review 4:** Not raised as a finding. Review 4 considered recovery bounded; this is a separate monitor-error path.

### 2. TWAI and SPP edge-case regression coverage is incomplete

- **Severity:** Medium
- **Exact reference:** [`tests/twai_supervisor/main/test_twai_supervisor.c`](./tests/twai_supervisor/main/test_twai_supervisor.c), tests at lines 194, 250, and 344; [`tests/spp_writer/main/test_bt_spp_writer.c`](./tests/spp_writer/main/test_bt_spp_writer.c), timeout/disconnect cases at lines 940 and 1101; [`main/bt_spp_writer.c`](./main/bt_spp_writer.c), write-event matching at lines 1001–1020.
- **Evidence:** The TWAI suite has three top-level cases: startup, successful recovery, and recovery-initiation exhaustion. It does not exercise missing recovery-complete alert/deadline expiry, restart exhaustion, or persistent alert-read failure. The writer test rejects a late completion after disconnect when no write is active, but does not test a stale completion arriving while a new write is active after the same handle is reused. The production write-event matcher correlates by handle; session generation is not present in the callback parameters.
- **Practical impact:** Review 4’s requested completion-timeout and stale-session assurances are not fully demonstrated by the present focused suites. The current session and writer module tests are useful but are not an end-to-end test of the `bt_spp.c` callback coordinator or rapid close/reopen ordering.
- **Recommended correction:** Add targeted tests for recovery timeout and restart failure, persistent alert-read failure, and close/reopen with reused handle plus delayed old completion. Exercise session reset, writer notification, and event handling together; confirm the stack’s callback-order/handle-reuse guarantees before relying on handle-only completion matching.
- **Review 4:** **Partially resolved.** R4-02’s broad missing-test gap is substantially closed by the new projects and reported 72 passes, but these specific failure paths remain unverified.

### 3. Application peer authorization and protocol-version contract remain undefined

- **Severity:** Medium
- **Exact reference:** [`main/bt_spp.c`](./main/bt_spp.c), discoverable scan configuration at lines 493–498 and authenticated SPP server at lines 516–520; [`main/singlecan_commands.c`](./main/singlecan_commands.c), strict request schema at lines 146–276.
- **Evidence:** The server is generally discoverable and requests SPP link authentication. The request schema has no protocol-version field or application-level peer authorization/allowlist. Session ownership limits concurrent clients but does not identify an approved client.
- **Practical impact:** An unintended paired/authenticated peer may occupy the command session and submit valid configuration and command envelopes. Since handlers are stubs and no mapping is installed, this is not presently a path to CAN actuation; it remains a security gate before mappings or state-changing handlers are introduced.
- **Recommended correction:** Decide and implement the trusted-peer enrollment/authorization and protocol-version policy before enabling vehicle actions; verify unauthorized peers are rejected before command processing.
- **Review 4:** **Still partially resolved from Review 3 M2.** Per-session rate limiting is now present; authorization and version negotiation remain unresolved and were retained as pre-mapping work in Review 4.

### 4. Response enqueue is not delivery; retry and side-effect semantics are undefined

- **Severity:** Medium
- **Exact reference:** [`main/singlecan_commands.c`](./main/singlecan_commands.c), request ID is recorded before command processing at lines 823–830 and response calls at lines 845–912; [`main/singlecan_responses.c`](./main/singlecan_responses.c), SPP enqueue result at lines 150–176; [`main/bt_spp.h`](./main/bt_spp.h), queue-only meaning of `ESP_OK` at lines 119–123.
- **Evidence:** Responses are queued, not confirmed delivered. Queue/write failures are counted and can disconnect a session, but the command processor does not act on its response-send result, and an ID is consumed before the command response is queued. A retry of that ID within the session is rejected as a duplicate rather than replaying a cached outcome.
- **Practical impact:** The client can time out without knowing whether a future state-changing command was applied. A retry/cached-result or commit-acknowledgement contract has not been specified. Current stubs do not perform side effects, so this is not a current CAN-safety defect.
- **Recommended correction:** Before adding side effects, define command acceptance/commit/response-loss semantics and make retries idempotent or return a cached result for repeated request IDs. Keep enqueue success distinct from delivery success.
- **Review 4:** **Partially resolved / carried forward.** Review 4 noted bounded failure handling and response loss (Review 3 M1); counters and disconnect behavior exist, but no delivery, retry, or idempotency contract is established.

### 5. Project analysis remains materially stale

- **Severity:** Low
- **Exact reference:** [`PROJECT_ANALYSIS.md`](./PROJECT_ANALYSIS.md), especially lines 5–34, 90–122, and 298–363.
- **Evidence:** It still describes active Wi-Fi/TCP/HTTP services, source files absent from the repository, raw CAN forwarding, and TCP command paths. The current firmware is Classic-SPP/JSON, drains CAN without forwarding it, and has no active TCP/Wi-Fi/HTTP project components.
- **Practical impact:** Maintainers can make incorrect architecture, security, or integration decisions based on nonexistent services and outdated data-flow descriptions.
- **Recommended correction:** Refresh or clearly archive the analysis so it describes the current SPP-only, non-transmitting phase and its actual build/test layout.
- **Review 4:** **Unresolved.** Review 4 explicitly retained this as L1 documentation drift.

## Review 4 disposition

| Review 4 item | Status in Review 5 |
|---|---|
| **R4-01 — ESP-IDF version not enforced** | **Resolved for production.** Root [`CMakeLists.txt`](./CMakeLists.txt) checks for exactly 5.3.1 before loading the project. The supplied positive/negative enforcement evidence is consistent with this code. |
| **R4-02 — No automated protocol/lifecycle/recovery/CAN-safety tests** | **Partially resolved.** Isolated projects and hardware-run results now cover substantial behavior, including parser/replay/rate handling, framing, ownership, writer failures/timeouts, recovery, and no-unmapped-transmit. See Finding 2 for missing failure edges and integration coverage. |
| Review 4’s carried Review 3 **M1** response delivery/retry semantics | **Partially resolved; residual contract gap.** See Finding 4. |
| Review 4’s carried Review 3 **M2** peer authorization/versioning | **Partially resolved; authorization/version remain.** See Finding 3. |
| Review 4 **L1** stale project analysis | **Unresolved.** See Finding 5. |

The three isolated test roots [`tests/spp_writer/CMakeLists.txt`](./tests/spp_writer/CMakeLists.txt), [`tests/twai_supervisor/CMakeLists.txt`](./tests/twai_supervisor/CMakeLists.txt), and [`tests/can_invariant/CMakeLists.txt`](./tests/can_invariant/CMakeLists.txt) do not independently enforce the 5.3.1 version, unlike production and the other test roots. This does not invalidate the reported results if run in the stated environment, but means those projects alone do not enforce the same toolchain constraint.

## Safety and readiness assessment

- **Review 4 issues:** R4-01 is **resolved**; R4-02 is **partially resolved**. The carried delivery and authorization concerns remain partial; stale documentation remains unresolved.
- **CAN safety claims:** The 72 reported tests support the specific exercised cases and are a significant improvement. They do **not** adequately support every current safety claim by themselves: recovery-completion timeout/restart failure, persistent alert-read failure, and integrated same-handle stale completion behavior are not covered by the inspected focused tests. The source still provides a strong current guard: only the private verified-frame path calls `twai_transmit()`, no mapping is compiled in, and current handlers are stubs.
- **Next phase:** The branch is ready for further non-transmitting protocol and integration work, but **not ready to enable verified vehicle mappings or state-changing CAN actions**.
- **Remaining work, priority order:**
  1. Make failed TWAI health monitoring unable to remain `RUNNING`; test persistent alert-read failure.
  2. Add the missing TWAI timeout/restart and integrated SPP stale-event/reused-handle tests.
  3. Define and implement peer authorization and protocol-version policy before mappings.
  4. Specify idempotent retry and response/side-effect semantics before any handler performs an action.
  5. Replace or mark the stale `PROJECT_ANALYSIS.md`; consider enforcing ESP-IDF 5.3.1 in all standalone test projects.

**Overall verdict: READY FOR NON-TRANSMITTING DEVELOPMENT; NOT READY FOR VERIFIED CAN-MAPPING INTEGRATION.** No current unverified CAN transmission path was found.
