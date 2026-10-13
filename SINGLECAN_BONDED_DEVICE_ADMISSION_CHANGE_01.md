# SingleCAN Bonded-Device Admission Change 01

## 1. Cause of the Fold7 rejection
`ESP_SPP_SRV_OPEN_EVT` in `main/bt_spp.c` called `bt_peer_authorization_is_explicitly_trusted()`. That function requires the peer address to equal the single address stored in NVS (the Fold5) before it checks the bond list. A bonded Fold7 failed the NVS comparison and was rejected.

## 2. Files changed
- `main/bt_spp.c` (admission call, variable name, log wording)
- `tests/bt_peer_authorization/main/test_bt_peer_authorization.c` (one test added and registered)
- `tests/spp_coordinator_lifecycle/main/test_bt_spp_coordinator_lifecycle.c` (one test added and registered)
- `SINGLECAN_BONDED_DEVICE_ADMISSION_CHANGE_01.md` (this report)

## 3. Old policy
Peer must match the single NVS-stored address and also be in the Classic Bluetooth bond database.

## 4. New policy
`bt_peer_authorization_is_trusted()`: the peer address must currently be in the ESP-IDF Classic Bluetooth bond list. NULL, zero, unbonded, empty-list, allocation-failure and bond-list query errors all fail closed. Any bonded device is admitted, subject to the single-session rule.

## 5. Why pairing is still required
Admission only succeeds for addresses in the bond database. The firmware does not bond devices automatically, and unbonded devices are rejected and disconnected via the existing rejected-client path.

## 6. Why only one phone controls SingleCAN
The bonded check is followed unchanged by `bt_spp_accept_session_transition()`, which holds the lifecycle mutex and the one-active-client ownership. A second bonded client is refused and goes through rejected-client disconnect handling.

## 7. Replacement after disconnect
After the active session closes and cleanup completes (command-session reset, SPP frame reset, writer disconnect), the session is free. A different bonded phone then obtains a new session with a new session generation.

## 8. Stale events
Data, close, congestion and write events are filtered by handle and session generation. Events from a rejected handle or the previous client do not match the new active session and are ignored. This logic was not modified.

## 9. Tests added
- `test_any_bonded_peer_is_trusted_without_nvs_binding` (bt_peer_authorization): two different bonded addresses are both authorized, with no NVS access. Existing tests already cover unbonded, no-bond, null, zero and bond-list-error rejection.
- `test_second_client_rejected_then_replacement_after_close` (spp_coordinator_lifecycle): a second client cannot replace the active session; stale-handle close and congestion events from the rejected client do not affect the active session; after close a different handle gets a new session ID; stale close and congestion from the old handle do not affect the new session.

## 10. Explicit NVS APIs
`bt_peer_authorization_is_explicitly_trusted()`, `bt_peer_authorization_load_trusted_address()` and the related store APIs and their tests are retained unchanged.

## 11. Protocol and CAN behavior
Unchanged. No protocol handshake, session identity, command rate limiting, writer or CAN code was touched.

## 12. Tests to run
- `tests/bt_peer_authorization`
- `tests/spp_coordinator_lifecycle`
- Other existing SPP host/target tests as usual (not run here; builds and tests were not executed).

## 13. Diff summary
- `main/bt_spp.c`: `bt_peer_authorization_is_explicitly_trusted` -> `bt_peer_authorization_is_trusted`; `peer_is_trusted` -> `peer_is_bonded`; reject log says "unbonded".
- Two test files: one new test each, registered in `app_main`/runner.
