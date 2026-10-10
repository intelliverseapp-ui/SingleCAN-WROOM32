# SingleCAN-WROOM32 — Copilot Review 7

**Review date:** 2026-10-10

**Branch / commit:** `singlecan-spp-session-tests` / `f6e0c3412ccfdb66015fd75762d385eb81874992`
**Scope:** Entire tracked repository at the requested commit: production source and headers, all tracked test projects and defaults, repository configuration and documentation, Reviews 5/6, and relevant implementation history. This review adds only this report.

## Executive summary

The current firmware preserves its intentional no-transmit boundary. The verified-frame lookup in [main/singlecan_can.c:48](main/singlecan_can.c#L48) rejects every currently defined command; the only `twai_transmit()` call is private to that module at [main/singlecan_can.c:173](main/singlecan_can.c#L173). All command handlers remain non-transmitting stubs. I found no present client-controlled path that can send an unverified CAN frame.

Review 6's three runtime findings are closed at their stated scope: writer force-close now atomically matches handle and session generation; configured peer addresses are reconciled into NVS and still require a Bluetooth bond; and failed rejected-client disconnect requests now have bounded asynchronous retries. The supplied ESP-IDF 5.3.1 build/startup and focused test results are consistent with the current sources.

This fresh review identified two new SPP concurrency defects. The first is a gap between generation-matched session invalidation and the unscoped cleanup of command, framer, and writer state. A new client can be accepted during that gap and have its state reset by the old writer task. The second is a check-then-disconnect race in rejected-client retry work: a numeric handle can become an authorized replacement between the active-handle check and the disconnect request. Both affect connection availability and session correctness, not the current CAN transmission boundary.

**Overall recommendation: CONDITIONAL GO.** Continued non-transmitting development can continue, but I recommend fixing the two confirmed SPP races and adding deterministic regressions before merging this branch, even for that phase. **NO-GO for enabling verified CAN mappings or state-changing command handlers.** The remaining coordinator-level test gap, response-loss/idempotency contract, deployment documentation, and future TWAI transmit/fault atomicity must be resolved before those features are enabled.

## Release recommendations

| Scope | Recommendation | Rationale |
|---|---|---|
| Continued non-transmitting development firmware | **CONDITIONAL GO** | The firmware remains fail-closed for CAN transmission, but the new SPP races can interrupt a newly accepted session or disconnect its replacement. Fix and regression-test those before merging this branch. |
| Enabling verified CAN mappings | **NO-GO** | No mappings currently exist, which is an intentional safety boundary. Before adding one, define and test how a terminal TWAI fault is serialized against actual frame submission; the current state checks and subsequent transmit are not atomic. |
| Enabling state-changing command handlers | **NO-GO** | Define retry, response-loss, duplicate-request, and idempotency semantics; add production-coordinator SPP tests; and fix the SPP lifecycle races. A successful response enqueue is not proof of peer delivery. |

## Review 6 finding status

| Review 6 finding | Status | Current disposition |
|---|---|---|
| **M1 — A stale writer failure could force-close a replacement SPP session** | **CLOSED** | Commit `9c93b6c` added `bt_spp_session_force_close_if_matches()`, which compares handle and generation and clears session ownership under the session lock. The old unconditional force-close path is gone. A different, newly identified race remains in the cleanup performed *after* this conditional close; see New Finding M1. |
| **M2 — Fresh NVS had no production trusted-peer provisioning path** | **CLOSED** | Commit `3056dac` added the Kconfig address, strict parser, reconciliation, and production initialization call. Missing state is provisioned from the explicit configuration; conflicting or unreadable state fails initialization; admission still requires the peer to be bonded. Empty configuration remains deliberately fail-closed. |
| **L1 — Rejected-client disconnect errors had no bounded follow-up handling** | **CLOSED** | Commit `f6e0c34` adds an immediate request, queued retries with a bounded attempt count, duplicate-handle token replacement, and close-event cancellation. The worker has a new check-then-act identity race (New Finding M2); the closed finding's missing retry path is nevertheless implemented. |

## Review 5 finding status

| Review 5 finding | Status | Current disposition |
|---|---|---|
| **R5-1 — Unexpected TWAI alert-read failures leave health reported as RUNNING** | **CLOSED** | `can_health_task()` counts consecutive non-timeout read failures and enters terminal fault at [main/tasks.c:692](main/tasks.c#L692). The supplied persistent-read-failure test links production `tasks.c` and verifies that recovery/restart do not occur after fault. |
| **R5-2 — TWAI/SPP edge-case regression coverage incomplete** | **PARTIALLY CLOSED** | The supplied TWAI timeout, restart-exhaustion, and alert-read-failure tests and the writer's ordered same-handle reuse regression extend coverage. SPP tests still compile isolated writer/session/rejected-client modules, not `main/bt_spp.c` or the callback coordinator; actual callback sequencing, the new retry TOCTOU, and old-writer cleanup racing with a replacement open are not integrated tests. |
| **R5-3 — Application peer authorization and protocol-version contract undefined** | **CLOSED** | The production SPP open path checks explicit trusted-peer authorization at [main/bt_spp.c:586](main/bt_spp.c#L586), and command parsing requires protocol version 1 at [main/singlecan_commands.c:699](main/singlecan_commands.c#L699). The configured address is reconciled during authorization initialization at [main/bt_peer_authorization.c:391](main/bt_peer_authorization.c#L391). The deployment sequence still needs clearer documentation; see Defensive suggestions. |
| **R5-4 — Response enqueue is not delivery; retry and side-effect semantics undefined** | **PARTIALLY CLOSED** | The completion-driven writer, bounded queue, timeout, congestion, cancellation, and failure counters improve transport behavior. They do not give the client a delivery guarantee or define replay/cached-result behavior for a future command with side effects. No side-effecting CAN handler is currently implemented. |
| **R5-5 — `PROJECT_ANALYSIS.md` materially stale** | **OPEN** | [PROJECT_ANALYSIS.md:5](PROJECT_ANALYSIS.md#L5) still describes Wi-Fi/TCP startup and [PROJECT_ANALYSIS.md:20](PROJECT_ANALYSIS.md#L20) lists source modules that are absent from this repository. Its CAN forwarding and command-path descriptions also do not match the current SPP-only, non-transmitting firmware. |

## New findings

### Critical

None identified.

### High

None identified.

### Medium

#### M1 — Old writer cleanup can reset a replacement session after a generation-matched close

- **Classification:** Confirmed concurrency defect
- **Confidence:** 8/10
- **Exact references:** [main/bt_spp.c:267](main/bt_spp.c#L267), [main/bt_spp.c:272](main/bt_spp.c#L272), [main/bt_spp.c:274](main/bt_spp.c#L274), [main/bt_spp.c:276](main/bt_spp.c#L276), [main/bt_spp.c:592](main/bt_spp.c#L592), [main/bt_spp.c:612](main/bt_spp.c#L612)
- **Source-level evidence:** On a failed stack disconnect request, the writer task atomically invalidates the old `(handle, session_id)` at line 267. It then resets the command processor, receive framer, and writer without holding any lifecycle serialization or checking that the old generation still owns those resources. The SPP callback can concurrently accept a new session at line 592 and initialize those same shared resources at line 612. `singlecan_commands` and `bt_spp_framer` hold module-global session state; `bt_spp_writer_on_disconnected()` resets the queue and raises a write-failure signal.
- **Concrete runtime failure scenario:** An old session's writer times out; `esp_spp_disconnect()` fails; the conditional close succeeds. Before the writer task finishes the three cleanup calls, the Bluetooth callback accepts a new client. The old task then resets the new client's command/request-ID state and framer and clears its queued output or signals its in-flight writer as failed.
- **Impact:** Availability and session correctness. A valid new client can lose responses, have a partially received frame discarded, or have its per-session request/configuration state reset unexpectedly. The present non-transmitting handlers prevent this from becoming a current CAN actuation path.
- **Smallest safe remediation:** Serialize session acceptance and all teardown side effects as one lifecycle transition, or make each cleanup operation generation-aware and ensure a prior generation cannot reset resources after a replacement is admitted. Merely making the session close atomic is insufficient.
- **Required regression tests:** Deterministically interleave old-generation failed-disconnect cleanup with a new `ESP_SPP_SRV_OPEN_EVT`; prove the new command/framer state, queued response, and in-flight writer remain intact. Also test the same interleaving with the same numeric handle and with a different handle.

#### M2 — Rejected-client retry can disconnect a newly authorized connection reusing the handle

- **Classification:** Confirmed concurrency defect
- **Confidence:** 7/10
- **Exact references:** [main/bt_spp_rejected_client.c:292](main/bt_spp_rejected_client.c#L292), [main/bt_spp_rejected_client.c:311](main/bt_spp_rejected_client.c#L311), [main/bt_spp_rejected_client.c:312](main/bt_spp_rejected_client.c#L312), [main/bt_spp_rejected_client.c:549](main/bt_spp_rejected_client.c#L549), [main/bt_spp.c:633](main/bt_spp.c#L633)
- **Source-level evidence:** The retry worker checks that its `(handle, token)` is still tracked, then separately compares only the current active numeric handle. It later calls `s_disconnect_callback(handle)` without passing or revalidating the token. A close event stops tracking by numeric handle, but cannot revoke work that has already passed both checks. Thus neither the token nor active-session comparison is bound atomically to the stack disconnect request.
- **Concrete runtime failure scenario:** A retry for a rejected connection passes its tracking and active-handle checks while no authorized session owns the handle. The original connection closes, the close callback cancels the record, and a new trusted connection is admitted using the same numeric handle. If the retry task resumes after the new open and posts its disconnect request, it can disconnect the new authorized connection. The retry worker and Bluetooth callback run in separate task contexts, so the checks and request are not serialized by the callback queue.
- **Impact:** Availability and peer-session integrity. A stale rejected-client retry can tear down a valid authorized connection. It does not by itself authorize rejected data or transmit CAN frames.
- **Smallest safe remediation:** Serialize retry disconnect requests with SPP close/open lifecycle transitions, or quarantine a handle until all work for its prior connection is drained before accepting a reused handle. A second best-effort numeric-handle check narrows the race but does not establish identity; the request must not outlive the connection identity it was created for.
- **Required regression tests:** Add a coordinator-level deterministic interleaving that pauses a retry after its active-handle check, closes the original rejected session, accepts a trusted same-handle replacement, and resumes the retry. Assert no disconnect request targets the replacement. Cover duplicate-handle token replacement and close cancellation under the same schedule.

### Low

No new confirmed low-severity findings.

## Confirmed defects versus defensive suggestions

The two Medium findings above are source-level concurrency defects with concrete cross-task interleavings. The following are **defensive suggestions, not confirmed defects**:

1. **Supervise accepted disconnect requests through close completion.** An initial `esp_spp_disconnect()` result of `ESP_OK` returns immediately at [main/bt_spp_rejected_client.c:501](main/bt_spp_rejected_client.c#L501); tracking is only created after a request error. The retry-success path keeps its tracking until a close event, but the initial-success path has no corresponding pending record. If the stack accepts a request but the connection does not close or reports an unsuccessful close, there is no retry/escalation path. Consider tracking all rejected handles until confirmed close, with a bounded completion deadline. Test accepted-request/no-close and unsuccessful-close outcomes before treating this as a defect; the current supplied evidence does not establish that ESP-IDF 5.3.1 omits the close event in these cases.
2. **Make retry exhaustion and capacity loss observable as a degraded service state.** Exhausted retries and retry-queue/slot failures log and stop tracking at [main/bt_spp_rejected_client.c:344](main/bt_spp_rejected_client.c#L344), [main/bt_spp_rejected_client.c:366](main/bt_spp_rejected_client.c#L366), and [main/bt_spp_rejected_client.c:529](main/bt_spp_rejected_client.c#L529). This is bounded and fails closed for command admission, but a stack link that remains open after all disconnect attempts fail could continue consuming Bluetooth resources. Whether that needs a terminal/degraded state depends on the ESP-IDF disconnect completion contract and operational recovery policy; do not replace bounded retries with an unbounded loop.
3. **Document and enforce the trusted-peer deployment sequence.** The empty configured address is deliberately allowed by [main/Kconfig.projbuild:13](main/Kconfig.projbuild#L13), while [sdkconfig.defaults](sdkconfig.defaults) does not supply a peer address. With the default, Bluetooth initialization and SPP readiness can succeed while every client is rejected; this matches the stated fail-closed development/manufacturing behavior. Release images should explicitly set the address and document how it becomes bonded. NVS erase on `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND` is handled at [main/main.c:68](main/main.c#L68); after such an erase, the configured address can be provisioned again, but the lost bond still prevents admission until the peer is bonded again. Current code fails closed on configuration/NVS reconciliation errors and does not overwrite a conflicting stored address.
4. **Complete initialization rollback before adding retry/reinitialization.** `bt_spp_init()` starts the rejected-client queue/task before allocating readiness and writer resources at [main/bt_spp.c:785](main/bt_spp.c#L785), [main/bt_spp.c:800](main/bt_spp.c#L800), and [main/bt_spp.c:809](main/bt_spp.c#L809). A later failure leaves prior resources alive, and a second initialization attempt will not recreate an already-running rejected-client worker. The current production `app_main()` enters a terminal fault loop on initialization failure, so this is not a demonstrated repeated-retry leak in the current startup flow. Add unwind/cleanup semantics before introducing retry or runtime restart.
5. **Enforce the production ESP-IDF version in standalone test projects.** The production root checks for exactly ESP-IDF 5.3.1, while isolated test roots build their own projects. All tracked test defaults target ESP32 and no generated `sdkconfig` files are tracked; the supplied test evidence is stated to come from 5.3.1. Consider adding a common version guard to test roots so later isolated test runs cannot silently use another API/toolchain version.
6. **Close the future TWAI transmit/fault race before installing mappings.** [main/singlecan_can.c:428](main/singlecan_can.c#L428) rechecks that TWAI is `RUNNING` before calling the private transmitter, but the health monitor may transition to `FAULTED` after that check and before [main/singlecan_can.c:173](main/singlecan_can.c#L173). No current command reaches the transmitter because mapping lookup rejects every value, so this is a pre-mapping safety design/test requirement, not a current defect.

## Missing tests

1. **Production SPP coordinator integration:** Compile and exercise `main/bt_spp.c` with controllable ESP-IDF callback/event doubles for server open, close, data, congestion, and write events. Current isolated tests link `bt_spp_writer.c`, `bt_spp_session.c`, or `bt_spp_rejected_client.c` independently; for example, [tests/spp_writer/main/CMakeLists.txt:1](tests/spp_writer/main/CMakeLists.txt#L1) links only the writer module, and [tests/spp_rejected_client/main/CMakeLists.txt:1](tests/spp_rejected_client/main/CMakeLists.txt#L1) links only rejected-client recovery.
2. **Replacement-session cleanup race:** Exercise M1 with a writer-task cleanup interleaved against an authorized open and verify all new-generation state is preserved.
3. **Rejected retry versus same-handle reuse:** Exercise M2 at the actual callback coordinator boundary, not only by calling the rejected-client module with a stub active-handle getter.
4. **Rejected disconnect completion and resource failure:** Inject `ESP_OK` without a close event, close failure status, retry-slot exhaustion, retry-queue saturation, and final-attempt failure. Verify the selected degraded/fail-closed policy and ensure stale work cannot target a replacement.
5. **Trusted peer from production startup to admission:** Exercise configured and empty configuration, NVS missing/corrupt/conflict/commit failure, post-erase bond loss, peer pairing/bond state, and the resulting `ESP_SPP_SRV_OPEN_EVT` path together. Current peer-authorization tests use mocked NVS and Bluetooth bond APIs and do not execute this full coordinator path.
6. **Response-loss and future side effects:** Before implementing state-changing handlers, test a command accepted with a lost response, same-ID retry, new-ID retry, duplicate requests, and cached or idempotent outcomes. Current request IDs are recorded before command processing at [main/singlecan_commands.c:878](main/singlecan_commands.c#L878), while response enqueue is not peer-delivery confirmation.
7. **TWAI transmit/fault serialization:** Before adding verified mappings, inject a terminal health transition between the last health check and transmit and verify the explicitly selected safe behavior. Current mappings are intentionally absent, so this is not currently testable as a mapped action.

## Prioritized next-step checklist

1. Fix M1 by serializing or generation-scoping teardown side effects; add a deterministic replacement-open regression.
2. Fix M2 by binding rejected disconnect work to the original connection identity and serializing it against close/open/reuse; add a deterministic same-handle regression.
3. Add a production `bt_spp.c` coordinator test covering open, close, stale data, congestion, writes, and rejected-client recovery.
4. Define and test rejected-disconnect completion behavior, retry exhaustion, capacity-loss handling, and service-health reporting.
5. Refresh or explicitly archive `PROJECT_ANALYSIS.md` so it describes the current Classic SPP, non-transmitting architecture.
6. Document trusted-peer provisioning, bonding, NVS erase/recovery, and the expected behavior of an empty trusted-peer configuration; ensure release builds set a deliberate address.
7. Define response-loss, duplicate-request, and idempotency semantics before implementing state-changing command handlers.
8. Define and test the TWAI health-to-transmission synchronization boundary before adding verified CAN mappings.

## Final disposition

- **Are all confirmed Review 6 runtime defects closed?** **Yes, at the exact scope of the three Review 6 findings.** The generation-matched force-close fixes M1's unconditional replacement-session close; configured peer reconciliation fixes M2's missing production provisioning path; bounded retries fix L1's absence of follow-up after a rejected disconnect request fails. This review's two new SPP concurrency findings are distinct residual defects and do not reopen those exact findings.
- **Is another code change required before this branch should be merged for continued non-transmitting development?** **Yes.** Fix the two Medium SPP concurrency findings and add the deterministic regressions before merge. The current no-mapping/no-state-changing boundary prevents these findings from enabling CAN actuation, but does not make the connection lifecycle behavior reliable enough to merge without correction.
- **Evidence and validation:** The review used the supplied ESP-IDF 5.3.1 build, hardware startup, and test results as evidence; it did not rerun the firmware build or hardware suites. The current checkout was clean at `f6e0c34` before this report was created. No production code, tests, build files, or configuration were changed.
