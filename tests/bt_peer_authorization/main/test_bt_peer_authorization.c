#include "bt_peer_authorization.h"

#include "esp_gap_bt_api.h"
#include "unity.h"

#include <string.h>

#define MAX_TEST_BONDED_DEVICES 4

static int s_bonded_device_count =
    0;

static esp_err_t s_bond_list_result =
    ESP_OK;

static esp_bd_addr_t s_bonded_devices[
    MAX_TEST_BONDED_DEVICES
];

static int s_bond_count_query_count =
    0;

static int s_bond_list_query_count =
    0;

static void reset_bond_database(void)
{
    s_bonded_device_count =
        0;

    s_bond_list_result =
        ESP_OK;

    memset(
        s_bonded_devices,
        0,
        sizeof(s_bonded_devices)
    );

    s_bond_count_query_count =
        0;

    s_bond_list_query_count =
        0;
}

static void copy_address(
    esp_bd_addr_t destination,
    const esp_bd_addr_t source
)
{
    memcpy(
        destination,
        source,
        sizeof(esp_bd_addr_t)
    );
}

int __wrap_esp_bt_gap_get_bond_device_num(void)
{
    s_bond_count_query_count +=
        1;

    return s_bonded_device_count;
}

esp_err_t __wrap_esp_bt_gap_get_bond_device_list(
    int *device_count,
    esp_bd_addr_t *device_list
)
{
    s_bond_list_query_count +=
        1;

    if (s_bond_list_result != ESP_OK) {
        return s_bond_list_result;
    }

    if (
        device_count == NULL ||
        device_list == NULL ||
        *device_count <
            s_bonded_device_count
    ) {
        return ESP_ERR_INVALID_ARG;
    }

    for (
        int index = 0;
        index < s_bonded_device_count;
        ++index
    ) {
        copy_address(
            device_list[index],
            s_bonded_devices[index]
        );
    }

    *device_count =
        s_bonded_device_count;

    return ESP_OK;
}

static void test_init_accepts_accessible_empty_bond_database(void)
{
    reset_bond_database();

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_peer_authorization_init()
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_bond_count_query_count
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        s_bond_list_query_count
    );
}

static void test_init_fails_when_bond_count_query_fails(void)
{
    reset_bond_database();

    s_bonded_device_count =
        ESP_FAIL;

    TEST_ASSERT_EQUAL(
        ESP_FAIL,
        bt_peer_authorization_init()
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_bond_count_query_count
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        s_bond_list_query_count
    );
}

static void test_null_and_zero_addresses_are_rejected(void)
{
    reset_bond_database();

    s_bonded_device_count =
        1;

    const esp_bd_addr_t trusted_address = {
        0x10,
        0x20,
        0x30,
        0x40,
        0x50,
        0x60
    };

    copy_address(
        s_bonded_devices[0],
        trusted_address
    );

    const esp_bd_addr_t zero_address = {
        0,
        0,
        0,
        0,
        0,
        0
    };

    TEST_ASSERT_FALSE(
        bt_peer_authorization_is_trusted(
            NULL
        )
    );

    TEST_ASSERT_FALSE(
        bt_peer_authorization_is_trusted(
            zero_address
        )
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        s_bond_count_query_count
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        s_bond_list_query_count
    );
}

static void test_peer_is_rejected_when_no_bonded_devices_exist(void)
{
    reset_bond_database();

    const esp_bd_addr_t peer_address = {
        0x10,
        0x20,
        0x30,
        0x40,
        0x50,
        0x60
    };

    TEST_ASSERT_FALSE(
        bt_peer_authorization_is_trusted(
            peer_address
        )
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_bond_count_query_count
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        s_bond_list_query_count
    );
}

static void test_exact_bonded_peer_is_trusted(void)
{
    reset_bond_database();

    s_bonded_device_count =
        2;

    const esp_bd_addr_t first_address = {
        0x01,
        0x02,
        0x03,
        0x04,
        0x05,
        0x06
    };

    const esp_bd_addr_t trusted_address = {
        0x10,
        0x20,
        0x30,
        0x40,
        0x50,
        0x60
    };

    copy_address(
        s_bonded_devices[0],
        first_address
    );

    copy_address(
        s_bonded_devices[1],
        trusted_address
    );

    TEST_ASSERT_TRUE(
        bt_peer_authorization_is_trusted(
            trusted_address
        )
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_bond_count_query_count
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_bond_list_query_count
    );
}

static void test_unlisted_peer_is_rejected(void)
{
    reset_bond_database();

    s_bonded_device_count =
        1;

    const esp_bd_addr_t trusted_address = {
        0x10,
        0x20,
        0x30,
        0x40,
        0x50,
        0x60
    };

    const esp_bd_addr_t untrusted_address = {
        0x10,
        0x20,
        0x30,
        0x40,
        0x50,
        0x61
    };

    copy_address(
        s_bonded_devices[0],
        trusted_address
    );

    TEST_ASSERT_FALSE(
        bt_peer_authorization_is_trusted(
            untrusted_address
        )
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_bond_count_query_count
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_bond_list_query_count
    );
}

static void test_bond_list_query_error_fails_closed(void)
{
    reset_bond_database();

    s_bonded_device_count =
        1;

    s_bond_list_result =
        ESP_FAIL;

    const esp_bd_addr_t peer_address = {
        0x10,
        0x20,
        0x30,
        0x40,
        0x50,
        0x60
    };

    TEST_ASSERT_FALSE(
        bt_peer_authorization_is_trusted(
            peer_address
        )
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_bond_count_query_count
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_bond_list_query_count
    );
}

void app_main(void)
{
    UNITY_BEGIN();

    RUN_TEST(
        test_init_accepts_accessible_empty_bond_database
    );

    RUN_TEST(
        test_init_fails_when_bond_count_query_fails
    );

    RUN_TEST(
        test_null_and_zero_addresses_are_rejected
    );

    RUN_TEST(
        test_peer_is_rejected_when_no_bonded_devices_exist
    );

    RUN_TEST(
        test_exact_bonded_peer_is_trusted
    );

    RUN_TEST(
        test_unlisted_peer_is_rejected
    );

    RUN_TEST(
        test_bond_list_query_error_fails_closed
    );

    UNITY_END();
}
