#include "bt_peer_authorization.h"

#include "esp_gap_bt_api.h"
#include "nvs.h"
#include "unity.h"

#include <string.h>

#define MAX_TEST_BONDED_DEVICES 4
#define TEST_NVS_HANDLE 17

static esp_err_t s_nvs_open_result =
    ESP_OK;

static esp_err_t s_nvs_get_blob_result =
    ESP_ERR_NVS_NOT_FOUND;

static esp_err_t s_nvs_set_blob_result =
    ESP_OK;

static esp_err_t s_nvs_commit_result =
    ESP_OK;

static esp_bd_addr_t s_nvs_trusted_address = {
    0
};

static size_t s_nvs_blob_length =
    sizeof(esp_bd_addr_t);

static int s_nvs_open_count =
    0;

static int s_nvs_get_blob_count =
    0;

static int s_nvs_set_blob_count =
    0;

static int s_nvs_commit_count =
    0;

static int s_nvs_close_count =
    0;

static nvs_open_mode_t s_last_nvs_open_mode =
    NVS_READONLY;

static void reset_nvs_store(void)
{
    s_nvs_open_result =
        ESP_OK;

    s_nvs_get_blob_result =
        ESP_ERR_NVS_NOT_FOUND;

    s_nvs_set_blob_result =
        ESP_OK;

    s_nvs_commit_result =
        ESP_OK;

    memset(
        s_nvs_trusted_address,
        0,
        sizeof(s_nvs_trusted_address)
    );

    s_nvs_blob_length =
        sizeof(esp_bd_addr_t);

    s_nvs_open_count =
        0;

    s_nvs_get_blob_count =
        0;

    s_nvs_set_blob_count =
        0;

    s_nvs_commit_count =
        0;

    s_nvs_close_count =
        0;

    s_last_nvs_open_mode =
        NVS_READONLY;
}

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
    reset_nvs_store();

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

esp_err_t __wrap_nvs_open(
    const char *namespace_name,
    nvs_open_mode_t open_mode,
    nvs_handle_t *out_handle
)
{
    s_nvs_open_count +=
        1;

    s_last_nvs_open_mode =
        open_mode;

    if (
        namespace_name == NULL ||
        out_handle == NULL
    ) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_nvs_open_result != ESP_OK) {
        return s_nvs_open_result;
    }

    *out_handle =
        TEST_NVS_HANDLE;

    return ESP_OK;
}

esp_err_t __wrap_nvs_get_blob(
    nvs_handle_t handle,
    const char *key,
    void *out_value,
    size_t *length
)
{
    s_nvs_get_blob_count +=
        1;

    if (
        handle != TEST_NVS_HANDLE ||
        key == NULL ||
        length == NULL
    ) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_nvs_get_blob_result != ESP_OK) {
        return s_nvs_get_blob_result;
    }

    if (
        out_value == NULL
    ) {
        *length =
            s_nvs_blob_length;

        return ESP_OK;
    }

    if (
        *length <
            s_nvs_blob_length
    ) {
        *length =
            s_nvs_blob_length;

        return ESP_ERR_NVS_INVALID_LENGTH;
    }

    memcpy(
        out_value,
        s_nvs_trusted_address,
        s_nvs_blob_length
    );

    *length =
        s_nvs_blob_length;

    return ESP_OK;
}

esp_err_t __wrap_nvs_set_blob(
    nvs_handle_t handle,
    const char *key,
    const void *value,
    size_t length
)
{
    s_nvs_set_blob_count +=
        1;

    if (
        handle != TEST_NVS_HANDLE ||
        key == NULL ||
        value == NULL
    ) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_nvs_set_blob_result != ESP_OK) {
        return s_nvs_set_blob_result;
    }

    if (
        length !=
            sizeof(esp_bd_addr_t)
    ) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(
        s_nvs_trusted_address,
        value,
        sizeof(esp_bd_addr_t)
    );

    s_nvs_blob_length =
        sizeof(esp_bd_addr_t);

    s_nvs_get_blob_result =
        ESP_OK;

    return ESP_OK;
}

esp_err_t __wrap_nvs_commit(
    nvs_handle_t handle
)
{
    s_nvs_commit_count +=
        1;

    if (handle != TEST_NVS_HANDLE) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }

    return s_nvs_commit_result;
}

void __wrap_nvs_close(
    nvs_handle_t handle
)
{
    if (handle == TEST_NVS_HANDLE) {
        s_nvs_close_count +=
            1;
    }
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

static void test_load_rejects_null_destination(void)
{
    reset_bond_database();

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        bt_peer_authorization_load_trusted_address(
            NULL
        )
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        s_nvs_open_count
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        s_nvs_get_blob_count
    );
}

static void test_load_reports_missing_trusted_address(void)
{
    reset_bond_database();

    esp_bd_addr_t loaded_address = {
        0
    };

    TEST_ASSERT_EQUAL(
        ESP_ERR_NVS_NOT_FOUND,
        bt_peer_authorization_load_trusted_address(
            loaded_address
        )
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_open_count
    );

    TEST_ASSERT_EQUAL(
        NVS_READONLY,
        s_last_nvs_open_mode
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_get_blob_count
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_close_count
    );
}

static void test_load_returns_exact_stored_address(void)
{
    reset_bond_database();

    const esp_bd_addr_t stored_address = {
        0x10,
        0x20,
        0x30,
        0x40,
        0x50,
        0x60
    };

    copy_address(
        s_nvs_trusted_address,
        stored_address
    );

    s_nvs_get_blob_result =
        ESP_OK;

    s_nvs_blob_length =
        sizeof(esp_bd_addr_t);

    esp_bd_addr_t loaded_address = {
        0
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_peer_authorization_load_trusted_address(
            loaded_address
        )
    );

    TEST_ASSERT_EQUAL_MEMORY(
        stored_address,
        loaded_address,
        sizeof(esp_bd_addr_t)
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_open_count
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_get_blob_count
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_close_count
    );
}

static void test_load_rejects_wrong_sized_address(void)
{
    reset_bond_database();

    const esp_bd_addr_t stored_address = {
        0x10,
        0x20,
        0x30,
        0x40,
        0x50,
        0x60
    };

    copy_address(
        s_nvs_trusted_address,
        stored_address
    );

    s_nvs_get_blob_result =
        ESP_OK;

    s_nvs_blob_length =
        sizeof(esp_bd_addr_t) - 1;

    esp_bd_addr_t loaded_address = {
        0xff,
        0xff,
        0xff,
        0xff,
        0xff,
        0xff
    };

    const esp_bd_addr_t zero_address = {
        0
    };

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_SIZE,
        bt_peer_authorization_load_trusted_address(
            loaded_address
        )
    );

    TEST_ASSERT_EQUAL_MEMORY(
        zero_address,
        loaded_address,
        sizeof(esp_bd_addr_t)
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_close_count
    );
}

static void test_store_rejects_invalid_addresses(void)
{
    reset_bond_database();

    const esp_bd_addr_t zero_address = {
        0
    };

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        bt_peer_authorization_store_trusted_address(
            NULL
        )
    );

    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        bt_peer_authorization_store_trusted_address(
            zero_address
        )
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        s_nvs_open_count
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        s_nvs_set_blob_count
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        s_nvs_commit_count
    );
}

static void test_store_commits_exact_trusted_address(void)
{
    reset_bond_database();

    const esp_bd_addr_t trusted_address = {
        0x10,
        0x20,
        0x30,
        0x40,
        0x50,
        0x60
    };

    TEST_ASSERT_EQUAL(
        ESP_OK,
        bt_peer_authorization_store_trusted_address(
            trusted_address
        )
    );

    TEST_ASSERT_EQUAL(
        NVS_READWRITE,
        s_last_nvs_open_mode
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_open_count
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_set_blob_count
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_commit_count
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_close_count
    );

    TEST_ASSERT_EQUAL_MEMORY(
        trusted_address,
        s_nvs_trusted_address,
        sizeof(esp_bd_addr_t)
    );
}

static void test_store_failure_skips_commit_and_closes_handle(void)
{
    reset_bond_database();

    s_nvs_set_blob_result =
        ESP_FAIL;

    const esp_bd_addr_t trusted_address = {
        0x10,
        0x20,
        0x30,
        0x40,
        0x50,
        0x60
    };

    TEST_ASSERT_EQUAL(
        ESP_FAIL,
        bt_peer_authorization_store_trusted_address(
            trusted_address
        )
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_open_count
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_set_blob_count
    );

    TEST_ASSERT_EQUAL_INT(
        0,
        s_nvs_commit_count
    );

    TEST_ASSERT_EQUAL_INT(
        1,
        s_nvs_close_count
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

    RUN_TEST(
        test_load_rejects_null_destination
    );

    RUN_TEST(
        test_load_reports_missing_trusted_address
    );

    RUN_TEST(
        test_load_returns_exact_stored_address
    );

    RUN_TEST(
        test_load_rejects_wrong_sized_address
    );

    RUN_TEST(
        test_store_rejects_invalid_addresses
    );

    RUN_TEST(
        test_store_commits_exact_trusted_address
    );

    RUN_TEST(
        test_store_failure_skips_commit_and_closes_handle
    );

    UNITY_END();
}
