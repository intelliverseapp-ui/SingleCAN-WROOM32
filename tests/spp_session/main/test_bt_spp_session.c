#include "bt_spp_session.h"

#include "unity.h"

#include <stdint.h>

static void test_first_client_acquires_session_ownership(void)
{
    bt_spp_session_init();

    uint32_t session_id =
        0;

    const int accepted =
        bt_spp_session_accept(
            100,
            &session_id
        );

    TEST_ASSERT_TRUE(
        accepted
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        session_id
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        session_id,
        bt_spp_session_get_id()
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_handle_is_active(
            100
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_matches(
            100,
            session_id
        )
    );
}

static void test_second_client_cannot_replace_session_owner(void)
{
    bt_spp_session_init();

    uint32_t first_session_id =
        0;

    const int first_accepted =
        bt_spp_session_accept(
            100,
            &first_session_id
        );

    TEST_ASSERT_TRUE(
        first_accepted
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        first_session_id
    );

    uint32_t second_session_id =
        0xA5A5A5A5;

    const int second_accepted =
        bt_spp_session_accept(
            200,
            &second_session_id
        );

    TEST_ASSERT_FALSE(
        second_accepted
    );

    TEST_ASSERT_EQUAL_HEX32(
        0xA5A5A5A5,
        second_session_id
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        first_session_id,
        bt_spp_session_get_id()
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_handle_is_active(
            100
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_handle_is_active(
            200
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_matches(
            100,
            first_session_id
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            200,
            first_session_id
        )
    );
}


static void test_unrelated_close_cannot_disconnect_owner(void)
{
    bt_spp_session_init();

    uint32_t session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            100,
            &session_id
        )
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        session_id
    );

    const int unrelated_close_result =
        bt_spp_session_close(
            200
        );

    TEST_ASSERT_FALSE(
        unrelated_close_result
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        session_id,
        bt_spp_session_get_id()
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_handle_is_active(
            100
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_handle_is_active(
            200
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_matches(
            100,
            session_id
        )
    );
}


static void test_owner_close_clears_session_and_advances_generation(void)
{
    bt_spp_session_init();

    uint32_t original_session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            100,
            &original_session_id
        )
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        original_session_id
    );

    const int close_result =
        bt_spp_session_close(
            100
        );

    TEST_ASSERT_TRUE(
        close_result
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_handle()
    );

    const uint32_t closed_session_id =
        bt_spp_session_get_id();

    TEST_ASSERT_NOT_EQUAL(
        original_session_id,
        closed_session_id
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        closed_session_id
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_handle_is_active(
            100
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            100,
            original_session_id
        )
    );
}


static void test_new_client_acquires_new_session_after_owner_close(void)
{
    bt_spp_session_init();

    uint32_t first_session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            100,
            &first_session_id
        )
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        first_session_id
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_close(
            100
        )
    );

    const uint32_t closed_session_id =
        bt_spp_session_get_id();

    TEST_ASSERT_NOT_EQUAL(
        first_session_id,
        closed_session_id
    );

    uint32_t second_session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            200,
            &second_session_id
        )
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        second_session_id
    );

    TEST_ASSERT_NOT_EQUAL(
        first_session_id,
        second_session_id
    );

    TEST_ASSERT_NOT_EQUAL(
        closed_session_id,
        second_session_id
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        200,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        second_session_id,
        bt_spp_session_get_id()
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_handle_is_active(
            100
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_handle_is_active(
            200
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            100,
            first_session_id
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            200,
            first_session_id
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_matches(
            200,
            second_session_id
        )
    );
}


static void test_zero_handle_is_rejected_without_state_change(void)
{
    bt_spp_session_init();

    uint32_t session_id =
        0xA5A5A5A5;

    const int accepted =
        bt_spp_session_accept(
            0,
            &session_id
        );

    TEST_ASSERT_FALSE(
        accepted
    );

    TEST_ASSERT_EQUAL_HEX32(
        0xA5A5A5A5,
        session_id
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_id()
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_handle_is_active(
            0
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            0,
            1
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_close(
            0
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_set_congested(
            0,
            1
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_id()
    );
}


static void test_null_session_id_pointer_is_rejected_without_state_change(void)
{
    bt_spp_session_init();

    const int accepted =
        bt_spp_session_accept(
            100,
            NULL
        );

    TEST_ASSERT_FALSE(
        accepted
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_id()
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_handle_is_active(
            100
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            100,
            1
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_close(
            100
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_id()
    );
}


static void test_only_active_owner_can_change_congestion(void)
{
    bt_spp_session_init();

    uint32_t session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            100,
            &session_id
        )
    );

    bt_spp_writer_session_t snapshot;

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_TRUE(
        snapshot.connected
    );

    TEST_ASSERT_FALSE(
        snapshot.congested
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_set_congested(
            100,
            1
        )
    );

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_TRUE(
        snapshot.congested
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_set_congested(
            200,
            0
        )
    );

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_TRUE(
        snapshot.congested
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        snapshot.handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        session_id,
        snapshot.session_id
    );

    TEST_ASSERT_TRUE(
        snapshot.server_ready
    );

    TEST_ASSERT_TRUE(
        snapshot.connected
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_set_congested(
            100,
            0
        )
    );

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_FALSE(
        snapshot.congested
    );

    TEST_ASSERT_TRUE(
        snapshot.connected
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        snapshot.handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        session_id,
        snapshot.session_id
    );
}


static void test_closing_congested_session_clears_writer_snapshot(void)
{
    bt_spp_session_init();

    uint32_t original_session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            100,
            &original_session_id
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_set_congested(
            100,
            1
        )
    );

    bt_spp_writer_session_t snapshot;

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        snapshot.handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        original_session_id,
        snapshot.session_id
    );

    TEST_ASSERT_TRUE(
        snapshot.server_ready
    );

    TEST_ASSERT_TRUE(
        snapshot.connected
    );

    TEST_ASSERT_TRUE(
        snapshot.congested
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_close(
            100
        )
    );

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        snapshot.handle
    );

    TEST_ASSERT_NOT_EQUAL(
        original_session_id,
        snapshot.session_id
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        snapshot.session_id
    );

    TEST_ASSERT_FALSE(
        snapshot.server_ready
    );

    TEST_ASSERT_FALSE(
        snapshot.connected
    );

    TEST_ASSERT_FALSE(
        snapshot.congested
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        snapshot.session_id,
        bt_spp_session_get_id()
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            100,
            original_session_id
        )
    );
}


static void test_force_close_clears_active_congested_session(void)
{
    bt_spp_session_init();

    uint32_t original_session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            100,
            &original_session_id
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_set_congested(
            100,
            1
        )
    );

    bt_spp_writer_session_t snapshot;

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        snapshot.handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        original_session_id,
        snapshot.session_id
    );

    TEST_ASSERT_TRUE(
        snapshot.server_ready
    );

    TEST_ASSERT_TRUE(
        snapshot.connected
    );

    TEST_ASSERT_TRUE(
        snapshot.congested
    );

    bt_spp_session_force_close();

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        snapshot.handle
    );

    TEST_ASSERT_NOT_EQUAL(
        original_session_id,
        snapshot.session_id
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        snapshot.session_id
    );

    TEST_ASSERT_FALSE(
        snapshot.server_ready
    );

    TEST_ASSERT_FALSE(
        snapshot.connected
    );

    TEST_ASSERT_FALSE(
        snapshot.congested
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        snapshot.session_id,
        bt_spp_session_get_id()
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_handle_is_active(
            100
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            100,
            original_session_id
        )
    );
}


static void test_new_client_acquires_new_session_after_force_close(void)
{
    bt_spp_session_init();

    uint32_t first_session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            100,
            &first_session_id
        )
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        first_session_id
    );

    bt_spp_session_force_close();

    const uint32_t forced_closed_session_id =
        bt_spp_session_get_id();

    TEST_ASSERT_NOT_EQUAL(
        first_session_id,
        forced_closed_session_id
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_handle()
    );

    uint32_t second_session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            200,
            &second_session_id
        )
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        second_session_id
    );

    TEST_ASSERT_NOT_EQUAL(
        first_session_id,
        second_session_id
    );

    TEST_ASSERT_NOT_EQUAL(
        forced_closed_session_id,
        second_session_id
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        200,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        second_session_id,
        bt_spp_session_get_id()
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_handle_is_active(
            100
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_handle_is_active(
            200
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            100,
            first_session_id
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            200,
            first_session_id
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            200,
            forced_closed_session_id
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_matches(
            200,
            second_session_id
        )
    );

    bt_spp_writer_session_t snapshot;

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_EQUAL_UINT32(
        200,
        snapshot.handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        second_session_id,
        snapshot.session_id
    );

    TEST_ASSERT_TRUE(
        snapshot.server_ready
    );

    TEST_ASSERT_TRUE(
        snapshot.connected
    );

    TEST_ASSERT_FALSE(
        snapshot.congested
    );
}


static void test_null_writer_snapshot_pointer_leaves_session_unchanged(void)
{
    bt_spp_session_init();

    uint32_t session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            100,
            &session_id
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_set_congested(
            100,
            1
        )
    );

    bt_spp_session_get_writer_snapshot(
        NULL
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        session_id,
        bt_spp_session_get_id()
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_handle_is_active(
            100
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_matches(
            100,
            session_id
        )
    );

    bt_spp_writer_session_t snapshot;

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        snapshot.handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        session_id,
        snapshot.session_id
    );

    TEST_ASSERT_TRUE(
        snapshot.server_ready
    );

    TEST_ASSERT_TRUE(
        snapshot.connected
    );

    TEST_ASSERT_TRUE(
        snapshot.congested
    );
}


static void test_init_resets_active_congested_session_to_initial_state(void)
{
    bt_spp_session_init();

    uint32_t original_session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            100,
            &original_session_id
        )
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        original_session_id
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_set_congested(
            100,
            1
        )
    );

    bt_spp_writer_session_t snapshot;

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        snapshot.handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        original_session_id,
        snapshot.session_id
    );

    TEST_ASSERT_TRUE(
        snapshot.server_ready
    );

    TEST_ASSERT_TRUE(
        snapshot.connected
    );

    TEST_ASSERT_TRUE(
        snapshot.congested
    );

    bt_spp_session_init();

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        snapshot.handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        snapshot.session_id
    );

    TEST_ASSERT_FALSE(
        snapshot.server_ready
    );

    TEST_ASSERT_FALSE(
        snapshot.connected
    );

    TEST_ASSERT_FALSE(
        snapshot.congested
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_id()
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_handle_is_active(
            100
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            100,
            original_session_id
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_set_congested(
            100,
            0
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_close(
            100
        )
    );
}


static void test_reused_handle_requires_new_session_generation(void)
{
    bt_spp_session_init();

    uint32_t first_session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            100,
            &first_session_id
        )
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        first_session_id
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_matches(
            100,
            first_session_id
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_close(
            100
        )
    );

    const uint32_t closed_session_id =
        bt_spp_session_get_id();

    TEST_ASSERT_NOT_EQUAL(
        first_session_id,
        closed_session_id
    );

    uint32_t second_session_id =
        0;

    TEST_ASSERT_TRUE(
        bt_spp_session_accept(
            100,
            &second_session_id
        )
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        second_session_id
    );

    TEST_ASSERT_NOT_EQUAL(
        first_session_id,
        second_session_id
    );

    TEST_ASSERT_NOT_EQUAL(
        closed_session_id,
        second_session_id
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        second_session_id,
        bt_spp_session_get_id()
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_handle_is_active(
            100
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            100,
            first_session_id
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            100,
            closed_session_id
        )
    );

    TEST_ASSERT_TRUE(
        bt_spp_session_matches(
            100,
            second_session_id
        )
    );

    bt_spp_writer_session_t snapshot;

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_EQUAL_UINT32(
        100,
        snapshot.handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        second_session_id,
        snapshot.session_id
    );

    TEST_ASSERT_TRUE(
        snapshot.server_ready
    );

    TEST_ASSERT_TRUE(
        snapshot.connected
    );

    TEST_ASSERT_FALSE(
        snapshot.congested
    );
}


static void test_force_close_while_idle_advances_generation_safely(void)
{
    bt_spp_session_init();

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_id()
    );

    bt_spp_session_force_close();

    const uint32_t first_forced_generation =
        bt_spp_session_get_id();

    TEST_ASSERT_NOT_EQUAL(
        0,
        first_forced_generation
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_handle()
    );

    bt_spp_writer_session_t snapshot;

    bt_spp_session_get_writer_snapshot(
        &snapshot
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        snapshot.handle
    );

    TEST_ASSERT_EQUAL_UINT32(
        first_forced_generation,
        snapshot.session_id
    );

    TEST_ASSERT_FALSE(
        snapshot.server_ready
    );

    TEST_ASSERT_FALSE(
        snapshot.connected
    );

    TEST_ASSERT_FALSE(
        snapshot.congested
    );

    bt_spp_session_force_close();

    const uint32_t second_forced_generation =
        bt_spp_session_get_id();

    TEST_ASSERT_NOT_EQUAL(
        first_forced_generation,
        second_forced_generation
    );

    TEST_ASSERT_NOT_EQUAL(
        0,
        second_forced_generation
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_is_connected()
    );

    TEST_ASSERT_EQUAL_UINT32(
        0,
        bt_spp_session_get_handle()
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_handle_is_active(
            100
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_matches(
            100,
            first_forced_generation
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_close(
            100
        )
    );

    TEST_ASSERT_FALSE(
        bt_spp_session_set_congested(
            100,
            1
        )
    );
}

void app_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(
        test_first_client_acquires_session_ownership
    );

    RUN_TEST(
        test_second_client_cannot_replace_session_owner
    );


    RUN_TEST(
        test_unrelated_close_cannot_disconnect_owner
    );


    RUN_TEST(
        test_owner_close_clears_session_and_advances_generation
    );


    RUN_TEST(
        test_new_client_acquires_new_session_after_owner_close
    );


    RUN_TEST(
        test_zero_handle_is_rejected_without_state_change
    );


    RUN_TEST(
        test_null_session_id_pointer_is_rejected_without_state_change
    );


    RUN_TEST(
        test_only_active_owner_can_change_congestion
    );


    RUN_TEST(
        test_closing_congested_session_clears_writer_snapshot
    );


    RUN_TEST(
        test_force_close_clears_active_congested_session
    );


    RUN_TEST(
        test_new_client_acquires_new_session_after_force_close
    );


    RUN_TEST(
        test_null_writer_snapshot_pointer_leaves_session_unchanged
    );


    RUN_TEST(
        test_init_resets_active_congested_session_to_initial_state
    );


    RUN_TEST(
        test_reused_handle_requires_new_session_generation
    );


    RUN_TEST(
        test_force_close_while_idle_advances_generation_safely
    );

    UNITY_END();
}
