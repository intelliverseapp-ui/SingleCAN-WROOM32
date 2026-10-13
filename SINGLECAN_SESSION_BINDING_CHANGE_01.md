# SINGLECAN_SESSION_BINDING_CHANGE_01

No builds or tests were run. BabyNodeAutomotive was not modified.

## 1. Files changed

Source:
- `main/bt_spp.c`, `main/bt_spp.h`
- `main/bt_spp_framer.c`, `main/bt_spp_framer.h`
- `main/singlecan_commands.c`, `main/singlecan_commands.h`
- `main/singlecan_responses.c`, `main/singlecan_responses.h`

Tests:
- `tests/protocol/main/test_singlecan_protocol.c`
- `tests/protocol/main/protocol_test_doubles.c`, `.h`
- `tests/responses/main/test_singlecan_responses.c`
- `tests/spp_framer/main/test_bt_spp_framer.c`
- `tests/spp_coordinator_lifecycle/main/test_bt_spp_coordinator_lifecycle.c` (stub signatures only)

## 2. Signature changes

- `singlecan_commands_process(const char *packet)` -> `(const char *packet, uint32_t session_id)`
- `singlecan_response_send(int packet_id, const char *status, ...)` -> `(int packet_id, uint32_t session_id, const char *status, const char *command, const char *reason)`
- `bt_spp_frame_handler_t`: `void (*)(const char *frame)` -> `void (*)(const char *frame, uint32_t session_id)`
- `bt_spp_framer_process(data, data_length)` -> `(data, data_length, uint32_t session_id)`
- `bt_spp_handle_complete_frame` (static in `bt_spp.c`) gains `uint32_t session_id`
- `process_json_packet` (static) gains `uint32_t origin_session_id`
- New: `esp_err_t bt_spp_send_for_session(const char *message, uint32_t session_id)`
- New static: `parse_session_id()` in `singlecan_commands.c`
- Test double: new `protocol_test_set_active_session()`; `bt_spp_session_get_id()` double is now settable (default 42).

## 3. Envelope contract

Allowed fields: `id, version, type, command, value, session_id`. Unknown fields and duplicates (including duplicate `session_id`) are rejected by `validate_object_schema`.
For every command other than `protocol.handshake`, `session_id` must be present exactly once, a JSON number, finite, integral, `>= 1`, `<= UINT32_MAX`, and equal to the SPP session that delivered the frame. Violations drop the packet silently (log only), consistent with other envelope failures.

## 4. Handshake exception

`protocol.handshake` need not carry `session_id`. If it does carry one, it is validated by the same rules. The successful response still returns the active nonzero `session_id` via `singlecan_handshake_response_send`, now using the originating session id.

## 5. Where origin identity is captured

In `bt_spp.c` `ESP_SPP_DATA_IND_EVT`: `bt_spp_session_get_writer_snapshot()` (the existing critical-section snapshot) is taken; the data is ignored unless `snapshot.handle == param->data_ind.handle` and `snapshot.session_id != 0`.

## 6. Propagation

`snapshot.session_id` -> `bt_spp_framer_process(..., session_id)` -> `deliver_complete_frame(session_id)` -> frame handler -> `singlecan_commands_process(frame, session_id)` -> `process_json_packet(packet, origin_session_id)` -> every `singlecan_response_send` / handshake response call. No later lookup supplies the identity. (The framer buffer is already reset on session accept/close, so partial frames do not cross sessions.)

## 7. Fail-closed handling

- `singlecan_commands_process` drops the packet (before rate limiting, parsing, request-ID recording, or any state change) when `session_id == 0` or `session_id != bt_spp_session_get_id()` (origin session no longer active). This lookup is used only to reject, never to label.
- After command-string validation and before the invalid-value/missing-value responses, request-ID freshness/recording, module config, and dispatch, the envelope `session_id` is parsed and compared to the origin session; missing/zero/non-integer/negative/fractional/out-of-range/mismatched all return after `cJSON_Delete`.

## 8. Responses

`singlecan_response_send` takes an explicit `session_id`, returns `ESP_ERR_INVALID_ARG` for 0, and serializes `"session_id"` right after `"id"`. All call sites in `singlecan_commands.c` pass `origin_session_id`.

## 9. No relabeling

The response session id is the value captured at the originating DATA_IND event. It is delivered to the writer through `bt_spp_send_for_session`, which returns `ESP_ERR_INVALID_STATE` if `session_id` is 0 or `bt_spp_session_matches(current_handle, session_id)` fails (and then `bt_spp_writer_send(message, session_id)` applies the existing writer session protections). A stale response is therefore dropped, never sent under a newer session id. The handshake response uses the same path.

## 10. Invalid identity vs. side effects

`process_module_config()`, `singlecan_command_dispatch()`, and `record_request_id()` are reached only after both the origin-active check and the envelope session check pass; both return earlier on failure.

## 11. Tests

Protocol (`test_singlecan_protocol.c`): all existing non-handshake fixtures now include `"session_id":42` and pass origin 42. New tests: matching session accepted; response carries originating session; missing, zero, malformed (string/fractional/negative/duplicate), out-of-range (4294967338), mismatched id rejected without response/config/dispatch; stale command not rebound to newer active session (both self-consistent stale and relabeled forms); request-ID replay still rejected with session_id present. Existing handshake test (no `session_id`, response session 42) retained.
Responses: new tests for `session_id` serialization and zero rejection; mock is now `bt_spp_send_for_session`.
Framer: tests pass and assert the session id through the handler. Coordinator lifecycle: stub signatures updated.

## 12. Preserved behavior

Unknown/duplicate-field rejection, request-ID replay protection, rate limiting (still first check once the origin is active), module validation, dispatch behavior, writer session checks, connection ownership/lifecycle, `bt_spp_send` (unchanged, no longer used by responses).

## 13. Limitations

- Check-then-send is not atomic across the session mutex; a session change between the active check and the send is caught by `bt_spp_send_for_session`/writer, not by command processing.
- Command state (config, request-ID history, rate limit) remains global and relies on reset at session transitions; it is not keyed per session.
- `bt_spp_send_for_session` and the DATA_IND capture are not covered by host unit tests (nothing was run).
- Android/client side must now send `session_id` after the handshake (not changed here).

## 14. Diff summary

14 files changed (8 source/header, 5 test files + the report). Core: envelope schema + validation in `singlecan_commands.c`, session threading through `bt_spp.c`/framer, explicit `session_id` in `singlecan_response_send`, new `bt_spp_send_for_session`.
