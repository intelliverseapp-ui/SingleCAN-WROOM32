# SingleCAN-WROOM32 — Copilot Review 8 (Corrected)

## Reviewed commit and repo state

- Reviewed commit: `900479e7854885ea6f9ac21649422c261ec5423f`
- Branch: `singlecan-spp-session-tests`
- Working-tree state:
  - `git status --short --branch` output: `## singlecan-spp-session-tests...origin/singlecan-spp-session-tests`
  - No tracked or untracked modifications were present in the working tree at review time.
- `git rev-parse HEAD`: `900479e7854885ea6f9ac21649422c261ec5423f`
- `git log -1 --oneline --decorate`: `900479e (HEAD -> singlecan-spp-session-tests, origin/singlecan-spp-session-tests) fix: guard rejected SPP disconnects against reuse`

## Required source verification checks

The current tracked source at the exact reviewed commit was verified directly:

- `main/bt_spp_rejected_client.h` exposes `bt_spp_rejected_client_disconnect_t` as `int (*)(uint32_t handle, esp_err_t *result)` and the guarded-disconnect contract is documented in [main/bt_spp_rejected_client.h](main/bt_spp_rejected_client.h#L11-L35).
- `bt_spp_rejected_client_init()` accepts a single guarded-disconnect callback in [main/bt_spp_rejected_client.h](main/bt_spp_rejected_client.h#L28-L35) and is called from [main/bt_spp.c](main/bt_spp.c#L978-L987).
- `main/bt_spp_rejected_client.c` contains no `active_handle_callback` or `handle_became_authorized` implementation; a repo search for those identifiers was empty in the tracked tree.
- `main/bt_spp.c` defines `bt_spp_disconnect_rejected_client()` at [main/bt_spp.c](main/bt_spp.c#L393-L425).
- That guarded callback locks the lifecycle mutex before checking active-session ownership and issues `esp_spp_disconnect()` while the mutex is still held, then releases it.
- Authorized session acceptance uses the same lifecycle mutex in [main/bt_spp.c](main/bt_spp.c#L300-L370).
- The coordinator registers `bt_spp_disconnect_rejected_client` via `bt_spp_rejected_client_init()` in [main/bt_spp.c](main/bt_spp.c#L978-L987).
- The rejected-client module no longer receives a raw `esp_spp_disconnect` direct callback from `bt_spp_init()`; the direct initialization path is replaced by the guarded callback contract in [main/bt_spp.c](main/bt_spp.c#L978-L987).
- The coordinator lifecycle regression includes `test_same_handle_replacement_waits_for_guarded_disconnect` in [tests/spp_coordinator_lifecycle/main/test_bt_spp_coordinator_lifecycle.c](tests/spp_coordinator_lifecycle/main/test_bt_spp_coordinator_lifecycle.c#L662-L815).
- The lifecycle harness links with `-Wl,--wrap=esp_spp_disconnect` in [tests/spp_coordinator_lifecycle/main/CMakeLists.txt](tests/spp_coordinator_lifecycle/main/CMakeLists.txt#L26).

## Executive summary

At the exact tracked commit `900479e`, the earlier stale Review 7 concurrency defects are closed in the current source. The repository now serializes session acceptance, close, generation-matched force-close, command reset, framer reset, and writer transition steps under a single lifecycle mutex, and the rejected-client recovery path is guarded against callback-time handle reuse. The no-transmit boundary remains intact: there is still no verified CAN mapping and no enabled state-changing command path in the current production implementation.

The branch is in a clean, reviewable state, and the current source contains the guarded-disconnect fix and the deterministic same-handle replacement regression required by the issue description. The remaining concerns on this branch are not current runtime defects; they are pre-mapping safety gates, documentation drift, and incomplete future-feature contract specification.

## Release recommendation

- Continued non-transmitting development: GO
- Verified CAN mappings: NO-GO
- State-changing command handlers: NO-GO

## Separate recommendations

### Continued non-transmitting development

Recommendation: GO.

Reason: the branch is clean, the guarded-disconnect fix is present, the lifecycle mutex serialization is active, and the production path remains fail-closed with no verified CAN transmit path.

### Enabling verified CAN mappings

Recommendation: NO-GO.

Reason: the current firmware intentionally does not enable CAN mappings and the safety boundary is maintained. This is a feature gate, not a defect, and the mapping/dispatch semantics remain intentionally disabled.

### Enabling state-changing command handlers

Recommendation: NO-GO.

Reason: future side-effect semantics, idempotency, duplicate-response handling, and TWAI transmit serialization remain outside the current no-transmit design and should not be enabled before explicit product gating and proof tests.

## Review 7 M1 and M2 re-evaluation from the current source

### Review 7 M1 — stale writer cleanup can reset a replacement session

Status: CLOSED at commit `900479e`.

Evidence:
- All session lifecycle transitions are serialized by the same mutex in [main/bt_spp.c](main/bt_spp.c#L300-L370).
- The old writer cleanup no longer races with authorized replacement acceptance in the deterministic same-handle regression at [tests/spp_coordinator_lifecycle/main/test_bt_spp_coordinator_lifecycle.c](tests/spp_coordinator_lifecycle/main/test_bt_spp_coordinator_lifecycle.c#L662-L815).
- The same-handle replacement regression blocks the old cleanup path until the guarded disconnect completes, then admits the replacement connection.

### Review 7 M2 — rejected-client retry can disconnect a newly authorized connection reusing the same handle

Status: CLOSED at commit `900479e`.

Evidence:
- The guarded callback signature changed to a single protected-handle check callback in [main/bt_spp_rejected_client.h](main/bt_spp_rejected_client.h#L11-L35).
- The callback in [main/bt_spp.c](main/bt_spp.c#L393-L425) acquires the lifecycle mutex before checking active-session ownership and calls `esp_spp_disconnect()` while the mutex remains held.
- The retry worker in [main/bt_spp_rejected_client.c](main/bt_spp_rejected_client.c#L238-L356) treats a zero return as a protected handle that must not be disconnected and cancels tracking instead of flagging a stack disconnect failure.
- The same-handle guarded-disconnect regression verifies the replacement is admitted only after the old disconnect completes in [tests/spp_coordinator_lifecycle/main/test_bt_spp_coordinator_lifecycle.c](tests/spp_coordinator_lifecycle/main/test_bt_spp_coordinator_lifecycle.c#L662-L815).

## Status of remaining Review 5 findings

| Review 5 finding | Status | Current disposition |
|---|---|---|
| R5-2 — TWAI/SPP edge-case regression coverage incomplete | PARTIALLY CLOSED | The current branch adds the exact lifecycle and guarded-disconnect regressions required by the previous concurrency issues, but future feature work still needs explicit mapping/dispatch and health-serialization coverage before broader product enablement. |
| R5-4 — Response enqueue is not delivery; retry and side-effect semantics are undefined | PARTIALLY CLOSED | The code still intentionally prohibits active CAN transmission and side-effecting handlers. This is not a current runtime defect, but the delivery/retry/idempotency contract remains a future feature requirement. |
| R5-5 — Project analysis remains materially stale | OPEN | [PROJECT_ANALYSIS.md](PROJECT_ANALYSIS.md) still contains materially stale architecture and service descriptions that do not match the current SPP-only, non-transmitting firmware. This is a documentation issue, not a live runtime defect. |

## New findings

### Critical

None.

### High

None.

### Medium

None.

### Low

None.

## Confirmed defects vs. defensive suggestions

### Confirmed defects

None found in the current tracked source at commit `900479e`.

### Defensive suggestions (not confirmed defects)

- Audit external callback reentry assumptions around `esp_spp_disconnect()` while the lifecycle mutex remains held. This should be validated against the ESP-IDF callback contract and any future callback semantics that re-enter the coordinator.
- Document the rejected-client retry exhaustion policy explicitly: when the bounded retry queue fills or the maximum attempts are reached, the module stops tracking the stale handle and logs the failure while the no-transmit boundary remains in place.
- Keep future TWAI transmit/health serialization gated behind explicit product-level testing before any mapping or command-path enablement.
- Treat [PROJECT_ANALYSIS.md](PROJECT_ANALYSIS.md) as an explicit documentation backlog item until it is refreshed to the current architecture.

## Missing tests

No production-blocking tests are missing before merge for continued non-transmitting development.

Recommended future tests before enabling mappings or state-changing handlers:

1. TWAI fault/health-to-transmit serialization test for future mapped transmit paths.
2. Future command idempotency and duplicate-response test when side effects exist.
3. Initialization rollback and repeated initialization test for SPP startup and rejected-client recovery.
4. End-to-end coordinator callback integration test for close/reopen ordering under reused handles and queued stale work.
5. Explicit documentation verification test or review step for architecture/docs drift.

## Final prioritized next-step checklist

1. Keep the no-transmit boundary active and do not enable verified mappings or state-changing handlers.
2. Preserve the lifecycle mutex serialization in the SPP coordinator and reject any future reintroduction of raw handle-only disconnect logic.
3. Maintain the rejected-client guarded-disconnect policy and keep the protected-handle rejection semantics in place.
4. Refresh [PROJECT_ANALYSIS.md](PROJECT_ANALYSIS.md) so the architecture and service description match the current repo state.
5. Add product-gating tests for future mapping/handler enablement before any non-testing feature expansion.

## Direct statements

- Both Review 7 concurrency defects are closed at commit `900479e`: Yes.
- Another code change is required before this branch can be merged for continued non-transmitting development: No.
- The current no-transmit boundary remains intact: Yes.
- Verified CAN mappings or state-changing command handlers may now be enabled: No.

## Conclusion

The current branch at `900479e` is in a good state for continued non-transmitting development and does not contain a current confirmed runtime defect that would justify a stop. The repository still intentionally enforces the no-transmit and no-side-effect boundary, and it should remain closed until explicit future safety work is completed and independently validated.
