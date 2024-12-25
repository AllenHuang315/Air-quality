#include "btstack_config.h"
#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/btstack_cyw43.h"
#include "pico/stdlib.h"
#include "gatt.h"
#include "ble_service.h"
#include "config/config.h"

#define MIN_CONN_INTERVAL 8     //10ms (8 * 1.25ms)
#define MAX_CONN_INTERVAL 16    //20ms
#define SLAVE_LATENCY 0
#define SUPERVISION_TIMEOUT 50   //500ms

static int le_notification_enabled;
static hci_con_handle_t con_handle = HCI_CON_HANDLE_INVALID;
static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_timer_source_t data_timer;
static repeating_timer_t led_timer;
static bool led_state = false;
static bool timer_setup = false;
static bool connection_params_updated = false;
static bool new_data_available = false;
static uint32_t last_send_time = 0;
static const uint32_t MIN_SEND_INTERVAL_MS = BME680_SAMPLE_PERIOD_MS - 100;

static sensor_data current_data;

#define APP_AD_FLAGS 0x06
static uint8_t adv_data[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, APP_AD_FLAGS,
    0x11, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
    0x0d, 0x46, 0x25, 0x4f, 0x0d, 0x7c, 0x66, 0x89,
    0x09, 0x40, 0x8e, 0xba, 0x22, 0xec, 0x85, 0x89
};
static const uint8_t adv_data_len = sizeof(adv_data);

bool led_blink_callback(repeating_timer_t *rt) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
    led_state = !led_state;
    return true;
}

static void start_led_blink(void) {
    add_repeating_timer_ms(500, led_blink_callback, NULL, &led_timer);
}

static void stop_led_blink(void) {
    cancel_repeating_timer(&led_timer);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

static void initialize_sensor_data(void) {
    memset(&current_data, 0, sizeof(sensor_data));
}

void update_sensor_data(sensor_data* data) {
    if (data != NULL) {
        memcpy(&current_data, data, sizeof(sensor_data));
        new_data_available = true;
        printf("Sensor data updated: temp=%.2f, humidity=%.2f, pressure=%.2f, gas=%.2f, voc=%.2f, pm25=%.2f\n",
               current_data.temperature, current_data.humidity, current_data.pressure,
               current_data.gas_resistance, current_data.voc_ppm, current_data.pm25);
    }
}

void send_sensor_data(void) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());

    if (current_time - last_send_time < MIN_SEND_INTERVAL_MS) {
        printf("Skipping send - too soon (interval: %lu ms)\n",
               current_time - last_send_time);
        return;
    }

    if (!le_notification_enabled || con_handle == HCI_CON_HANDLE_INVALID) {
        printf("Not sending: notifications %s, handle %s\n",
               le_notification_enabled ? "enabled" : "disabled",
               con_handle == HCI_CON_HANDLE_INVALID ? "invalid" : "valid");
        return;
    }

    if (!new_data_available) {
        printf("Skipping send - no new data\n");
        return;
    }

    printf("Sending sensor data buffer contents:\n");
    for(int i = 0; i < sizeof(sensor_data); i++) {
        printf("%02X ", ((uint8_t*)&current_data)[i]);
        if((i + 1) % 8 == 0) printf("\n");
    }
    printf("\n");

    printf("Notification state before send: %d\n", le_notification_enabled);

    int result = att_server_notify(con_handle,
                                 ATT_CHARACTERISTIC_2ce00ed4_b48a_4f0f_9dc9_34a71b75526b_01_VALUE_HANDLE,
                                 (uint8_t*)&current_data,
                                 sizeof(sensor_data));

    if (result == 0) {
        new_data_available = false;
        last_send_time = current_time;
    }

    printf("Notification send result: %d\n", result);
}

static void data_timer_handler(btstack_timer_source_t *ts) {
    printf("Timer triggered. Connected: %s, Notifications: %s, New data: %s\n",
           con_handle != HCI_CON_HANDLE_INVALID ? "yes" : "no",
           le_notification_enabled ? "enabled" : "disabled",
           new_data_available ? "yes" : "no");

    if (con_handle != HCI_CON_HANDLE_INVALID) {
        send_sensor_data();
    }

    if (con_handle != HCI_CON_HANDLE_INVALID) {
        btstack_run_loop_set_timer(ts, BME680_SAMPLE_PERIOD_MS);
        btstack_run_loop_add_timer(ts);
    } else {
        timer_setup = false;
    }
}

static void update_connection_parameters(hci_con_handle_t conn_handle) {
    printf("Updating connection parameters for handle: %04x\n", conn_handle);
    printf("Parameters: Interval %d-%d, Latency %d, Timeout %d\n",
           MIN_CONN_INTERVAL, MAX_CONN_INTERVAL, SLAVE_LATENCY, SUPERVISION_TIMEOUT);

    gap_update_connection_parameters(conn_handle,
                                   MIN_CONN_INTERVAL, MAX_CONN_INTERVAL,
                                   SLAVE_LATENCY, SUPERVISION_TIMEOUT);
}


static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(size);
    UNUSED(channel);

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event_type = hci_event_packet_get_type(packet);
    switch(event_type) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
            bd_addr_t local_addr;
            gap_local_bd_addr(local_addr);
            printf("BTstack running on %s\n", bd_addr_to_str(local_addr));

            uint16_t adv_int_min = 800;
            uint16_t adv_int_max = 800;
            uint8_t adv_type = 0;
            bd_addr_t null_addr;
            memset(null_addr, 0, 6);
            gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07, 0x00);
            gap_advertisements_set_data(adv_data_len, adv_data);
            gap_advertisements_enable(1);
            printf("Advertising started\n");
            start_led_blink();
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            con_handle = HCI_CON_HANDLE_INVALID;
            le_notification_enabled = 0;
            timer_setup = false;
            connection_params_updated = false;
            printf("Disconnected\n");
            gap_advertisements_enable(1);
            start_led_blink();
            break;

        case HCI_EVENT_LE_META:
            switch(hci_event_le_meta_get_subevent_code(packet)) {
                case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
                    con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                    printf("Connected\n");
                    stop_led_blink();

                    if (!connection_params_updated) {
                        printf("Updating connection parameters\n");
                        update_connection_parameters(con_handle);
                        connection_params_updated = true;
                    }

                    if (!timer_setup) {
                        printf("Setting up data timer\n");
                        btstack_run_loop_set_timer(&data_timer, 1000);
                        btstack_run_loop_set_timer_handler(&data_timer, &data_timer_handler);
                        btstack_run_loop_add_timer(&data_timer);
                        timer_setup = true;
                    }
                    break;

                case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
                    printf("Connection parameters updated\n");
                    uint16_t conn_interval = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
                    uint16_t conn_latency = hci_subevent_le_connection_update_complete_get_conn_latency(packet);
                    uint16_t conn_timeout = hci_subevent_le_connection_update_complete_get_supervision_timeout(packet);
                    printf("New parameters: interval %.2f ms, latency %u, timeout %u ms\n",
                           conn_interval * 1.25, conn_latency, conn_timeout * 10);
                    break;

                case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
                    printf("MTU exchange complete. New MTU size: %d\n",
                           att_event_mtu_exchange_complete_get_MTU(packet));
                break;
            }
            break;
    }
}

uint16_t att_read_callback(hci_con_handle_t connection_handle, uint16_t att_handle,
                          uint16_t offset, uint8_t *buffer, uint16_t buffer_size) {
    if (att_handle == ATT_CHARACTERISTIC_2ce00ed4_b48a_4f0f_9dc9_34a71b75526b_01_VALUE_HANDLE) {
        return att_read_callback_handle_blob((const uint8_t *)&current_data,
                                           sizeof(sensor_data), offset, buffer, buffer_size);
    }
    return 0;
}

int att_write_callback(hci_con_handle_t connection_handle, uint16_t att_handle,
                      uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size) {
    if (att_handle != ATT_CHARACTERISTIC_2ce00ed4_b48a_4f0f_9dc9_34a71b75526b_01_CLIENT_CONFIGURATION_HANDLE) {
        printf("Write to unexpected handle: %04X\n", att_handle);
        return 0;
    }

    if (buffer_size < 2) {
        printf("Invalid attribute value length: %d\n", buffer_size);
        return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
    }

    uint16_t configuration = little_endian_read_16(buffer, 0);
    printf("Client configuration value: %04X\n", configuration);

    le_notification_enabled = configuration ==
                            GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
    printf("Notifications %s (value: %04X)\n",
           le_notification_enabled ? "enabled" : "disabled",
           configuration);

    return 0;
}

int start_ble_service(void) {
    printf("Starting BLE service...\n");

    l2cap_init();
    sm_init();

    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    att_server_init(profile_data, att_read_callback, att_write_callback);
    att_server_register_packet_handler(packet_handler);

    initialize_sensor_data();

    if (hci_power_control(HCI_POWER_ON) != 0) {
        printf("HCI Power on failed\n");
        return -1;
    }

    printf("BLE service started\n");
    return 0;
}

void stop_ble_service(void) {
    printf("Stopping BLE service...\n");

    // 1. 停止資料傳輸定時器
    if (timer_setup) {
        btstack_run_loop_remove_timer(&data_timer);
        timer_setup = false;
    }

    // 2. 停止廣播
    gap_advertisements_enable(0);
    sleep_ms(50);  // 給予時間停止廣播

    // 3. 斷開現有連接
    if (con_handle != HCI_CON_HANDLE_INVALID) {
        printf("Disconnecting existing connection...\n");
        gap_disconnect(con_handle);
        sleep_ms(100);  // 等待斷開完成
    }

    // 4. 移除事件處理程序
    hci_remove_event_handler(&hci_event_callback_registration);

    // 5. 關閉 BLE 控制器
    hci_power_control(HCI_POWER_OFF);
    sleep_ms(100);  // 等待控制器完全關閉

    // 6. 清理所有狀態變數
    con_handle = HCI_CON_HANDLE_INVALID;
    le_notification_enabled = 0;
    timer_setup = false;
    connection_params_updated = false;
    new_data_available = false;

    printf("BLE service fully stopped and cleaned up\n");
}