#include <stdio.h>
#include <sys/_timespec.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "pico/sleep.h"
#include "tusb.h"
#include "sensors/bme680.h"
#include "pmsa003.h"
#include "lis3.h"
#include "ble_service.h"
#include "hardware/i2c.h"
#include "hardware/rtc.h"
#include "hardware/gpio.h"
#include "config/pin_config.h"
#include "config/config.h"
#include "hardware/structs/clocks.h"
#include "pico/runtime_init.h"
#include "pico/runtime.h"
#include "hardware/rosc.h"
#include "hardware/clocks.h"

// Constants for power management
#define SLEEP_TIMEOUT 10      // 10 minutes in seconds
#define SLEEP_CHECK_INTERVAL 10 // 30 seconds interval for sensor check in sleep mode
#define SENSOR_WARM_UP_DELAY_MS 180000

// Global variables for power management
static volatile bool awake = true;
static bool temp_check = false;
static bool movement_wake_up = false;

// Function declarations
static void sleep_callback(void);
static void accel_interrupt_handler(uint gpio, uint32_t events);
static void handle_time_overflow(datetime_t *time);
static void enter_sleep_mode(void);
static void leave_sleep_mode(void);
static bool is_abnormal(void);
static bool initialize_hardware(void);

int LIS3_operation_mode = 1;
air_quality_t data; // bme sensor data
uint16_t pm1_0, pm2_5, pm10;

sensor_data ble_data;
pmsa003_data_t pmsa_data;
uint32_t reading_count = 0;
absolute_time_t next_update;

typedef enum {
    PRE_WAKE, // Pre-wake for PM2.5 sensor
    FULL_WAKE // Wake from sleep mode
} WakeState;

static WakeState wake_state;

// Accelerometer interrupt handler
static void accel_interrupt_handler(uint gpio, uint32_t events) {
    //clear interrupt immediately
    gpio_acknowledge_irq(ACCEL_INT_PIN, GPIO_IRQ_EDGE_RISE);

    rtc_disable_alarm(); // disable any alarms

    if (gpio == ACCEL_INT_PIN) {
        gpio_put(PM25_SET_PIN, 1); // immediately turn PM2.5 sensor on
        movement_wake_up = true;
        printf("Movement detected!\n");

        //disable GPIO interrupt
        gpio_set_irq_enabled(ACCEL_INT_PIN, GPIO_IRQ_EDGE_RISE, false);
        disable_movement_detection(); // disable interrupt
    }
}

// RTC wake-up callback
static void sleep_callback(void) {
    if (wake_state == PRE_WAKE) {
        printf("Pre-wake: Turning on PM sensor...\n");
        gpio_put(PM25_SET_PIN, 1); // Turn on PM2.5 sensor

        // Get current time and calculate full wake time (15 seconds later)
        datetime_t current_time;
        rtc_get_datetime(&current_time);
        datetime_t t_full_wake = current_time;
        t_full_wake.sec += 15; // Full wake after giving pm 15 seconds to warm up
        handle_time_overflow(&t_full_wake);

        /*printf("Full wake alarm set for %04d-%02d-%02d %02d:%02d:%02d\n",
               t_full_wake.year, t_full_wake.month, t_full_wake.day,
               t_full_wake.hour, t_full_wake.min, t_full_wake.sec);*/

        // Set RTC alarm for full wake
        wake_state = FULL_WAKE;
        rtc_set_alarm(&t_full_wake, &sleep_callback);

    } else if (wake_state == FULL_WAKE) {
        printf("Full wake: Leaving sleep mode to do temp check...\n");
        temp_check = true; // Main logic gets triggered
    }
}

// Initialize hardware
static bool initialize_hardware(void) {
    stdio_init_all();
    sleep_ms(SERIAL_INIT_DELAY_MS); //delay for USB serial monitoring

    // initialize accelerometer interrupt pin
    gpio_init(ACCEL_INT_PIN);
    gpio_set_dir(ACCEL_INT_PIN, GPIO_IN);
    gpio_pull_down(ACCEL_INT_PIN);
    //gpio_set_irq_enabled_with_callback(ACCEL_INT_PIN, GPIO_IRQ_EDGE_RISE, true, &accel_interrupt_handler);

    // initialize the PM2.5 set pin
    gpio_init(PM25_SET_PIN);
    gpio_set_dir(PM25_SET_PIN, GPIO_OUT);
    gpio_put(PM25_SET_PIN, 1); // default sensor working state

    // Initialize CYW43 for LED control
    printf("Starting CYW43 initialization\n");
    if (cyw43_arch_init()) {
        printf("CYW43 init failed!\n");
        return -1;
    }
    printf("CYW43 initialized\n");

    // initialize i2c0 port
    i2c_init(i2c0, I2C0_FREQ);
    gpio_set_function(I2C0_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C0_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C0_SDA_PIN);
    gpio_pull_up(I2C0_SCL_PIN);

    // initialize sensors
    // initialize LIS3
    LIS3_init(i2c0, LIS3_operation_mode);
    sleep_ms(100);
    printf("LIS3DH initialized\n");

    // initialize BME680
    if (!bme680_init(i2c0)) {
        printf("BME680 initialization failed\n");
        return false;
    }
    sleep_ms(100);
    printf("BME680 initialized\n");

    // initialize pmsa003
    pmsa003_init(i2c1);
    printf("PMSA003 initialized\n");

    // Warm-up delay
    printf("Sensors are warming up. Please wait...\n");
    sleep_ms(SENSOR_WARM_UP_DELAY_MS);

    printf("Sensors warmed up and ready for operation.\n");

    // initialize BLE
    printf("Starting BLE service initialization...\n");
    if (start_ble_service() != 0) {
        printf("Failed to start BLE service\n");
        return false;
    }
    printf("BLE service started successfully\n");

    rtc_init();
    // Set a valid initial datetime to ensure RTC is running
    datetime_t initial_time = {
        .year  = 2024,
        .month = 1,
        .day   = 1,
        .dotw  = 1, // Monday
        .hour  = 0,
        .min   = 0,
        .sec   = 0
    };

    if (!rtc_set_datetime(&initial_time)) {
        printf("Failed to set initial RTC time!\n");
        return false;
    }

    // Enable RTC's interrupt
    rtc_enable_alarm();

    printf("RTC initialized successfully\n");
    return true;
}

static void handle_time_overflow(datetime_t *time) {
    if (time->sec >= 60) {
        time->sec -= 60;
        time->min += 1;
        if (time->min >= 60) {
            time->min -= 60;
            time->hour += 1;
            if (time->hour >= 24) {
                time->hour -= 24;
                time->day += 1;
                // Note: Handle month and year overflow if necessary
            }
        }
    }
}

static void enter_sleep_mode(void) {
    sleep_ms(1000);

    printf("Turning off PM sensor\n");
    gpio_put(PM25_SET_PIN, 0); // send PM2.5 sensor to sleep

    // Get current time
    datetime_t current_time;
    rtc_get_datetime(&current_time);
    /*printf("Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
           current_time.year, current_time.month, current_time.day,
           current_time.hour, current_time.min, current_time.sec);*/

    // Create pre-alarm time to wake up PM
    datetime_t t_pre_alarm = current_time;
    t_pre_alarm.sec += 20; // 10 seconds prior to wakeup timer to give pm sensor time to warm up
    handle_time_overflow(&t_pre_alarm);

    /*printf("Pre-wake alarm set for %04d-%02d-%02d %02d:%02d:%02d\n",
               t_pre_alarm.year, t_pre_alarm.month, t_pre_alarm.day,
               t_pre_alarm.hour, t_pre_alarm.min, t_pre_alarm.sec);*/

    // Reduce clock frequencies to save power while keeping BLE
    clock_configure(clk_peri, //clk_sys(cpu clock), clk_ref(clk_ref)
                   0,
                   CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                   12000000,  // Reduce to 12MHz
                   12000000);

    uart_default_tx_wait_blocking(); // Ensure message is sent

    enable_movement_detection();
    gpio_acknowledge_irq(ACCEL_INT_PIN, GPIO_IRQ_EDGE_RISE);
    gpio_set_irq_enabled_with_callback(ACCEL_INT_PIN, GPIO_IRQ_EDGE_RISE, true, &accel_interrupt_handler);

    // Set RTC alarm for pre wake
    wake_state = PRE_WAKE;
    rtc_set_alarm(&t_pre_alarm, &sleep_callback);
    //printf("RTC alarm set\n");

    // Enter light sleep mode using WFI
    printf("Entering light sleep mode (BLE stays active)...\n");
    awake = false;
    __wfi();  // Wait for interrupt while keeping BLE active
}

static void leave_sleep_mode(void) {
    rtc_disable_alarm(); // disable any alarms

    // Restore clock frequencies to full speed
    clock_configure(clk_peri,
                   0,
                   CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                   125000000,  // Restore to full speed
                   125000000);

    sleep_ms(100); // Allow clocks to stabilize

    // blink to signal wake up
    for (int i = 0; i < 5; i++) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(200);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(200);
    }

    // Reset next_update to 7 seconds after waking
    next_update = delayed_by_ms(get_absolute_time(), 7000);
}

static void BLE_send_data(void) {
    if (bme680_read_data(&data) || pmsa003_read_data(&pmsa_data)) {
        ble_data.temperature = data.temperature;
        ble_data.humidity = data.humidity;
        ble_data.pressure = data.pressure;
        ble_data.gas_resistance = data.gas_resistance;
        ble_data.voc_ppm = data.voc_ppm;
        ble_data.pm25 = (float) pmsa_data.pm2_5_env;
        update_sensor_data(&ble_data);
    }
}

static bool is_abnormal(void) {
    int num_checks = 5;          // Number of checks to confirm abnormality
    int delay_between_checks = 500;
    int abnormal_count = 0;

    printf("Checking for abnormal data...\n");

    for (int i = 0; i < num_checks; i++) {
        // Read sensor values directly
        bme680_read_data(&data);
        pmsa003_read_data(&pmsa_data);

        // Perform your abnormality logic here
        if ((float) pmsa_data.pm2_5_env > 12.0 || data.voc_ppm > 0.5) {
            abnormal_count++;
            /*printf("Check %d: Abnormal data detected\n", i + 1);
            } else {
                printf("Check %d: Data normal\n", i + 1);
            }*/
        }

        // Wait between checks (unless it's the last check)
        if (i < num_checks - 1) {
            sleep_ms(delay_between_checks);
        }
    }

    // Determine if abnormal data is confirmed
    if (abnormal_count >= (num_checks / 2) + 1) {
        //printf("Abnormal data confirmed after %d checks\n", num_checks);
        return true; // Abnormal data detected
    }

    //printf("Data deemed normal after %d checks\n", num_checks);
    return false; // Data is normal
}

int main() {
    if (!initialize_hardware()) {
        printf("Hardware initialization failed!\n");
        return -1;
    }

    sleep_ms(100);
    printf("Device initialized!\n");

    printf("Device is awake...blinking\n");
    for (int i = 0; i < 5; i++) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(200);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(200);
    }

    next_update = get_absolute_time();
    uint32_t counter = 0;

    while (true) {
        if (awake) {  // Active mode
            //int pin_state = gpio_get(ACCEL_INT_PIN);
            //printf("ACCEL_INT_PIN state: %d\n", pin_state);
            if (check_no_movement_for_duration()) {
                printf("No movement for 20 seconds, entering sleep mode.\n");1;
                enter_sleep_mode();
            } else {
                //printf("Movement checking\n");
                //periodically updating and sending sensor data through BLE
                if (absolute_time_diff_us(get_absolute_time(), next_update) <= 0) {
                    BLE_send_data();
                    counter++;
                    printf("\n=== Active Mode - Loop iteration %lu ===\n", counter);
                    next_update = delayed_by_ms(next_update, 7000); // set next time to send data
                }
            }
        } else {
            //printf("Device is asleep\n");
            if (movement_wake_up) {
                leave_sleep_mode();
                awake = true; // wake up device
                movement_wake_up = false; // disable flag
            } else if (temp_check) {
                // will blink LED 5 times after wake-up
                leave_sleep_mode();
                temp_check = false; // disable flag

                printf("Sending data over BLE\n");
                BLE_send_data();
                if (is_abnormal()) { // check for abnormal data
                    printf("Abnormal data detected, waking up\n");
                    // already awake so just set to awake mode
                    awake = true;
                } else {
                    // SEND BACK TO SLEEP
                    printf("No abnormal data detected, go back to sleep\n");
                    uart_default_tx_wait_blocking();
                    enter_sleep_mode();
                }
            }
        }
    }
    return 0;
}