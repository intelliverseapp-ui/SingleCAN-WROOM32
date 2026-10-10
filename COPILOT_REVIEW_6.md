# SingleCAN-WROOM32 — Copilot Review 6

**Review date:** 2026-10-10

**Branch / commit:** `singlecan-spp-session-tests` / `7430672fed7d10dc81b63c65a2a3157259811ad6`

**Scope:** Current tracked repository, production sources, test projects, Review 5, and relevant branch history. This review adds only this report; production code and tests were not modified. The supplied ESP-IDF 5.3.1 BTA/BTU/BTC queue behavior and hardware test/build outcomes were treated as review evidence.

## Executive summary

The current firmware remains fail-closed for CAN actuation: `singlecan_send_verified()` requires TWAI health to be `RUNNING`, the only verified-frame lookup rejects every current command, and the only `twai_transmit()` call is behind that lookup. The supplied production build succeeded. The specified TWAI persistent-alert failure, recovery-completion timeout, and restart-exhaustion results each pass; the SPP writer suite reports 13 passing tests.

Review 5 Finding 1 is closed by a bounded persistent alert-read failure path that enters terminal fault. The recovery timeout and restart-exhaustion paths are also present and directly exercise `main/tasks.c`. The latest writer regression meaningfully covers the ordered close / old-completion / reused-handle sequence. Explicit peer authorization is now enforced, and a mandatory protocol version was added in commit `2b39922`.

Two current SPP concerns remain. First, a generation check followed by an unconditional session force-close is not atomic; a concurrent close and same-handle reconnect can cause the new session to be cleared. Second, a clean NVS has no in-firmware trusted-peer enrollment path, so the server starts but cannot accept any client absent undocumented out-of-band provisioning. In addition, a failed disconnect request for a rejected peer is only logged, leaving no app-level retry or tracking path.

**Release recommendation: CONDITIONAL GO** for the current non-transmitting firmware and further test/development work. **NO-GO for enabling verified CAN mappings or state-changing actions** until the SPP teardown race is fixed, peer provisioning is explicitly established, and response/idempotency semantics are agreed. No current route from client-controlled commands to an unverified CAN transmission was found.

## Review 5 finding status

| Review 5 finding | Status | Current disposition |
|---|---|---|
| **R5-1 — Unexpected TWAI alert-read failures leave health `RUNNING`** | **CLOSED** | `can_health_task()` counts consecutive non-timeout read failures and calls the terminal-fault path at the configured threshold. The read-failure test compiles the production `main/tasks.c` and checks that health stays faulted and no recovery/restart is attempted. See [main/tasks.c:674](./main/tasks.c#L674) and [tests/twai_alert_read_failure/main/test_twai_supervisor.c:194](./tests/twai_alert_read_failure/main/test_twai_supervisor.c#L194). |
| **R5-2 — TWAI/SPP edge-case regression coverage incomplete** | **PARTIALLY CLOSED** | Separate projects now cover persistent read failure, recovery-completion timeout, and restart exhaustion; the latest writer regression covers ordered close, old-completion rejection, same-handle reuse with a new generation and same-length write, valid new completion, and duplicate rejection. The TWAI projects link production `main/tasks.c`; the SPP writer project links production `main/bt_spp_writer.c`. The remaining limitation is that the SPP project does not compile/exercise the `main/bt_spp.c` callback coordinator or real stack queues. Given the supplied ESP-IDF 5.3.1 FIFO queue/callback behavior, the tested ordered sequence is relevant, but it is not an end-to-end integration test. See [tests/spp_writer/main/CMakeLists.txt:1](./tests/spp_writer/main/CMakeLists.txt#L1), [tests/spp_writer/main/test_bt_spp_writer.c:1267](./tests/spp_writer/main/test_bt_spp_writer.c#L1267), and [tests/twai_restart_fault/main/test_twai_restart_fault.c:129](./tests/twai_restart_fault/main/test_twai_restart_fault.c#L129). |
| **R5-3 — Peer authorization and protocol-version contract undefined** | **PARTIALLY CLOSED** | Both subareas have advanced: SPP admission now requires a stored address that is still bonded, and command parsing requires protocol version `1` (added by `2b39922`, 2026-10-09). However, no production code provisions the stored address; an erased/fresh trusted-peer namespace therefore denies every client unless provisioning occurs out of band. That operational gap is a new finding below. See [main/bt_spp.c:580](./main/bt_spp.c#L580), [main/bt_peer_authorization.c:202](./main/bt_peer_authorization.c#L202), and [main/singlecan_commands.c:688](./main/singlecan_commands.c#L688). |
| **R5-4 — Response enqueue is not delivery; retry and side-effect semantics undefined** | **PARTIALLY CLOSED** | The completion-driven writer, bounded queue, failure counters, timeout handling, and session disconnect behavior constrain delivery failures. They do not establish that a queued response reached the peer, provide retry/cached outcomes for duplicate IDs, or define what happens when a future side effect succeeds but its response is lost. This remains a precondition to state-changing command handlers. See [main/bt_spp.h:114](./main/bt_spp.h#L114), [main/bt_spp.h:122](./main/bt_spp.h#L122), [main/singlecan_responses.c:151](./main/singlecan_responses.c#L151), and [main/singlecan_commands.c:848](./main/singlecan_commands.c#L848). |
| **R5-5 — `PROJECT_ANALYSIS.md` materially stale** | **OPEN** | The document still describes Wi-Fi/TCP/HTTP services and source files absent from the current firmware. See [PROJECT_ANALYSIS.md:5](./PROJECT_ANALYSIS.md#L5) and [PROJECT_ANALYSIS.md:20](./PROJECT_ANALYSIS.md#L20). |

## New findings

No Critical or High findings were identified.

### Medium

#### M1 — A stale writer failure can force-close a replacement SPP session

- **Classification:** Confirmed concurrency defect
- **Confidence:** 8/10
- **References:** [main/bt_spp.c:265](./main/bt_spp.c#L265), [main/bt_spp.c:271](./main/bt_spp.c#L271), [main/bt_spp_session.c:277](./main/bt_spp_session.c#L277)
- **Evidence:** On an `esp_spp_disconnect()` error, the writer bridge checks that `(handle, session_id)` still matches, releases the session lock when that check returns, and then calls `bt_spp_session_force_close()`. That force-close unconditionally clears whichever session is current; it does not compare the handle or generation under the same lock.
- **Concrete failure scenario:** A writer task handling a disconnect error confirms the old session is active. Before it calls `force_close()`, another task processes the old close and accepts a new client that reuses the same handle with a new generation. The old writer task then clears the new session and notifies the writer of a disconnection. The new physical connection can remain open while the application has forgotten its ownership, so its data is ignored and the client may need to reconnect.
- **Smallest safe remediation:** Add an atomic session operation that force-closes only if both handle and generation still match, and perform command/framer/writer resets only when that operation succeeds. Do not use handle-only or unconditional force-close for generation-scoped work.

#### M2 — Fresh NVS has no production trusted-peer provisioning path

- **Classification:** Confirmed implementation gap; deployment impact depends on whether external provisioning exists
- **Confidence:** 8/10
- **References:** [main/bt_peer_authorization.c:104](./main/bt_peer_authorization.c#L104), [main/bt_peer_authorization.c:157](./main/bt_peer_authorization.c#L157), [main/bt_peer_authorization.c:202](./main/bt_peer_authorization.c#L202), [main/bt_spp.c:930](./main/bt_spp.c#L930)
- **Evidence:** The trusted-address store function is defined, but has no production caller. Authorization denies when the NVS record is absent. Initialization only queries the bonded-device count and does not enroll or provision a trusted address.
- **Concrete failure scenario:** On a new unit with empty NVS, Bluetooth/SPP initialization can succeed, but every `ESP_SPP_SRV_OPEN_EVT` fails explicit trust and is disconnected. There is no in-firmware route to make the first peer trusted. An undocumented factory NVS-writing process could avoid this scenario, but no such process is present in the repository.
- **Smallest safe remediation:** Define and document a secure manufacturing or physical-presence-gated provisioning flow that stores the intended peer address. Preserve fail-closed behavior; do not treat the first arbitrary connection as trusted. Add a startup diagnostic that distinguishes “authorization ready with a provisioned peer” from “no peer provisioned.”

### Low

#### L1 — Rejected-client disconnect errors have no follow-up handling

- **Classification:** Confirmed incomplete error/recovery path
- **Confidence:** 7/10
- **References:** [main/bt_spp.c:593](./main/bt_spp.c#L593), [main/bt_spp.c:601](./main/bt_spp.c#L601), [main/bt_spp.c:610](./main/bt_spp.c#L610)
- **Evidence:** When an untrusted, additional, or invalid connection is rejected, a non-`ESP_OK` result from `esp_spp_disconnect()` is logged and the callback returns. The rejected handle is not retained, retried, or otherwise supervised.
- **Concrete failure scenario:** If the disconnect request fails and the stack leaves the ACL/SPP link open, data from that handle is correctly ignored because it does not own the session, but the link can continue consuming Bluetooth resources. Repeated failures or a peer that holds the link can impair legitimate connections.
- **Smallest safe remediation:** Retain rejected handles until a close event, retry disconnect with a bounded policy, and surface terminal failure if the stack cannot release the connection. Add an injected disconnect-error test. This finding concerns availability, not command admission: rejected data is not passed to the command processor.

## Confirmed defects vs. defensive suggestions

The three findings above describe source-level failure paths. Their runtime impact is conditional only where called out (external peer provisioning and stack disconnect failure).

The following are **defensive suggestions, not confirmed current defects**:

- Keep the no-mapping/no-state-changing-handler boundary in place. The health check and subsequent `twai_transmit()` are not one atomic operation, so before any mapping is enabled, establish how a concurrent transition to terminal TWAI fault prevents or safely rejects an in-progress transmit. No mapped command currently reaches that path.
- Add a coordinator-level SPP test that runs the actual close/open/write callback logic with controlled stack events, especially for M1 and failed rejected-client disconnects. Existing writer tests link the production writer module but not `bt_spp.c`.
- Refresh `PROJECT_ANALYSIS.md`; this is the still-open Review 5 low-severity documentation finding, not a newly discovered runtime defect.

## Missing tests

1. **Generation-conditional force-close race:** deterministically interleave old-session failure handling with close and same-handle/new-generation acceptance; prove the replacement session remains active.
2. **SPP admission coordinator:** exercise `ESP_SPP_SRV_OPEN_EVT` through `bt_spp.c` for trusted, untrusted, extra-client, invalid-status, and disconnect-error cases; verify rejected handles never reach the framer/command processor.
3. **Trusted-peer factory provisioning:** test empty NVS at startup, explicit secure provisioning, invalid/malformed address, and a correctly provisioned bonded peer through the production startup/admission path.
4. **Response-loss contract:** before adding side effects, test request-ID retry after response enqueue/write failure and verify the documented idempotency or cached-result behavior.
5. **TWAI transmit/fault boundary:** when a verified mapping exists, test a fault racing with command submission and prove the defined safety outcome. Current mapping lookup rejects all commands, so this cannot yet demonstrate mapped-command behavior.

## Final prioritized next-step checklist

1. Replace the check-then-unconditional force-close with an atomic `(handle, generation)` conditional close; add the race regression.
2. Specify and implement the trusted-peer provisioning process while retaining fail-closed admission on missing/corrupt trust state.
3. Add bounded rejected-client disconnect recovery and test the error path.
4. Add an SPP coordinator integration test using production callback code and the ESP-IDF 5.3.1 ordering assumptions.
5. Define response-loss, retry, and side-effect/idempotency semantics before enabling any state-changing handler.
6. Keep verified CAN mappings disabled until the transmit-vs-terminal-fault boundary has a tested behavior.
7. Update or explicitly archive `PROJECT_ANALYSIS.md`.

## Validation and scope notes

- The branch was clean and synchronized at the requested commit before this report was added.
- The review inspected current production code, focused test sources/configuration, recent relevant commit history, and the Review 5 report. No production build or hardware test was rerun for this documentation-only review.
- Test counts and production-build success stated in this report are the supplied results. The relevant test configurations were checked to ensure the TWAI supervisor and SPP writer tests compile the corresponding production modules.
- The supplied ESP-IDF 5.3.1 queue behavior was considered: BTA write/close requests share the BTU work queue, `osi_thread_post()` uses `xQueueSend()`, the worker consumes with `xQueueReceive()`, and SPP callbacks transfer through the same BTC queue. Public write events expose no internal `req_id`; the new regression tests the ordered old-completion rejection and same-handle/new-generation sequence.
