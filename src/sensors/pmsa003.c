#include "pmsa003.h"
#include <stdio.h>
#include <hardware/flash.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/binary_info.h"

static i2c_inst_t *i2c_port;

void pmsa003_init(i2c_inst_t *i2c_inst) {
    i2c_port = i2c_inst;

    i2c_init(i2c_port, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    bi_decl(bi_2pins_with_func(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C));

    sleep_ms(1000);
}

bool pmsa003_read_data(pmsa003_data_t *data) {
    uint8_t buffer[32];

    int ret = i2c_read_blocking(i2c_port, PMSA003I_I2C_ADDR, buffer, 32, false);

    if (ret != 32) {
        return false;
    }

    if (buffer[0] != 0x42 || buffer[1] != 0x4D) {
        return false;
    }

    uint16_t values[6];
    for (int i = 0; i < 6; i++) {
        values[i] = (buffer[4 + (i * 2)] << 8) | buffer[5 + (i * 2)];
    }

    data->pm1_0_standard = values[0];
    data->pm2_5_standard = values[1];
    data->pm10_standard = values[2];
    data->pm1_0_env = values[3];
    data->pm2_5_env = values[4];
    data->pm10_env = values[5];

    return true;
}
