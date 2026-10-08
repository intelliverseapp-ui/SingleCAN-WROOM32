#include "bt_spp_session.h"

#include "freertos/FreeRTOS.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static portMUX_TYPE s_session_lock =
    portMUX_INITIALIZER_UNLOCKED;

static uint32_t s_active_handle =
    0;

static uint32_t s_session_id =
    0;

static int s_connected =
    0;

static int s_congested =
    0;

// ------------------------------------------------------------
// INTERNAL SESSION-GENERATION HELPER
// ------------------------------------------------------------

static void advance_session_id_locked(void)
{
    s_session_id +=
        1;

    if (s_session_id == 0) {
        s_session_id =
            1;
    }
}

// ------------------------------------------------------------
// PUBLIC INITIALIZATION
// ------------------------------------------------------------

void bt_spp_session_init(void)
{
    portENTER_CRITICAL(
        &s_session_lock
    );

    s_active_handle =
        0;

    s_session_id =
        0;

    s_connected =
        0;

    s_congested =
        0;

    portEXIT_CRITICAL(
        &s_session_lock
    );
}

// ------------------------------------------------------------
// SINGLE-CLIENT SESSION ACCEPTANCE
// ------------------------------------------------------------

int bt_spp_session_accept(
    uint32_t handle,
    uint32_t *session_id
)
{
    if (
        handle == 0 ||
        session_id == NULL
    ) {
        return 0;
    }

    int accepted =
        0;

    uint32_t accepted_session_id =
        0;

    portENTER_CRITICAL(
        &s_session_lock
    );

    if (
        !s_connected &&
        s_active_handle == 0
    ) {
        advance_session_id_locked();

        s_active_handle =
            handle;

        s_connected =
            1;

        s_congested =
            0;

        accepted_session_id =
            s_session_id;

        accepted =
            1;
    }

    portEXIT_CRITICAL(
        &s_session_lock
    );

    if (accepted) {
        *session_id =
            accepted_session_id;
    }

    return accepted;
}

// ------------------------------------------------------------
// ACTIVE-SESSION MATCHING
// ------------------------------------------------------------

int bt_spp_session_handle_is_active(
    uint32_t handle
)
{
    if (handle == 0) {
        return 0;
    }

    int matches =
        0;

    portENTER_CRITICAL(
        &s_session_lock
    );

    matches =
        s_connected &&
        s_active_handle ==
            handle;

    portEXIT_CRITICAL(
        &s_session_lock
    );

    return matches;
}

int bt_spp_session_matches(
    uint32_t handle,
    uint32_t session_id
)
{
    if (
        handle == 0 ||
        session_id == 0
    ) {
        return 0;
    }

    int matches =
        0;

    portENTER_CRITICAL(
        &s_session_lock
    );

    matches =
        s_connected &&
        s_active_handle ==
            handle &&
        s_session_id ==
            session_id;

    portEXIT_CRITICAL(
        &s_session_lock
    );

    return matches;
}

// ------------------------------------------------------------
// ACTIVE-SESSION CONGESTION
// ------------------------------------------------------------

int bt_spp_session_set_congested(
    uint32_t handle,
    int congested
)
{
    if (handle == 0) {
        return 0;
    }

    int updated =
        0;

    portENTER_CRITICAL(
        &s_session_lock
    );

    if (
        s_connected &&
        s_active_handle ==
            handle
    ) {
        s_congested =
            congested
                ? 1
                : 0;

        updated =
            1;
    }

    portEXIT_CRITICAL(
        &s_session_lock
    );

    return updated;
}

// ------------------------------------------------------------
// ACTIVE-SESSION CLOSURE
// ------------------------------------------------------------

int bt_spp_session_close(
    uint32_t handle
)
{
    if (handle == 0) {
        return 0;
    }

    int closed =
        0;

    portENTER_CRITICAL(
        &s_session_lock
    );

    if (
        s_connected &&
        s_active_handle ==
            handle
    ) {
        s_connected =
            0;

        s_active_handle =
            0;

        s_congested =
            0;

        advance_session_id_locked();

        closed =
            1;
    }

    portEXIT_CRITICAL(
        &s_session_lock
    );

    return closed;
}

void bt_spp_session_force_close(void)
{
    portENTER_CRITICAL(
        &s_session_lock
    );

    s_connected =
        0;

    s_active_handle =
        0;

    s_congested =
        0;

    advance_session_id_locked();

    portEXIT_CRITICAL(
        &s_session_lock
    );
}

// ------------------------------------------------------------
// WRITER SESSION SNAPSHOT
// ------------------------------------------------------------

void bt_spp_session_get_writer_snapshot(
    bt_spp_writer_session_t *session
)
{
    if (session == NULL) {
        return;
    }

    memset(
        session,
        0,
        sizeof(*session)
    );

    portENTER_CRITICAL(
        &s_session_lock
    );

    session->handle =
        s_active_handle;

    session->session_id =
        s_session_id;

    /*
     * The SPP coordinator accepts a client only after the server
     * startup event has confirmed readiness. Therefore an active
     * session also represents a ready server for writer purposes.
     */
    session->server_ready =
        s_connected;

    session->connected =
        s_connected;

    session->congested =
        s_congested;

    portEXIT_CRITICAL(
        &s_session_lock
    );
}

// ------------------------------------------------------------
// PUBLIC SESSION ACCESSORS
// ------------------------------------------------------------

int bt_spp_session_is_connected(void)
{
    int connected;

    portENTER_CRITICAL(
        &s_session_lock
    );

    connected =
        s_connected;

    portEXIT_CRITICAL(
        &s_session_lock
    );

    return connected;
}

uint32_t bt_spp_session_get_handle(void)
{
    uint32_t handle;

    portENTER_CRITICAL(
        &s_session_lock
    );

    handle =
        s_active_handle;

    portEXIT_CRITICAL(
        &s_session_lock
    );

    return handle;
}

uint32_t bt_spp_session_get_id(void)
{
    uint32_t session_id;

    portENTER_CRITICAL(
        &s_session_lock
    );

    session_id =
        s_session_id;

    portEXIT_CRITICAL(
        &s_session_lock
    );

    return session_id;
}
