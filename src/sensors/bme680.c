#include <stdio.h>
#include "bme680.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "config/config.h"
#include <string.h>
#include <math.h>

static struct bme68x_dev bme;
static i2c_inst_t *i2c_port;

BME68X_INTF_RET_TYPE bme68x_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    int ret = i2c_write_blocking(i2c_port, BME680_I2C_ADDR, &reg_addr, 1, true);
    if (ret < 0) return ret;

    ret = i2c_read_blocking(i2c_port, BME680_I2C_ADDR, reg_data, len, false);
    return (ret < 0) ? ret : 0;
}

BME68X_INTF_RET_TYPE bme68x_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    uint8_t buf[len + 1];
    buf[0] = reg_addr;
    memcpy(buf + 1, reg_data, len);
    int ret = i2c_write_blocking(i2c_port, BME680_I2C_ADDR, buf, len + 1, false);
    return (ret < 0) ? ret : 0;
}

void bme68x_delay_us(uint32_t period, void *intf_ptr) {
    sleep_us(period);
}

bool bme680_init(i2c_inst_t *i2c_inst) {
    printf("Starting BME680 initialization...\n");
    i2c_port = i2c_inst;

    //I2C initialization
    /*i2c_init(i2c_port, BME680_I2C_FREQ);
    gpio_set_function(BME680_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(BME680_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(BME680_SDA_PIN);
    gpio_pull_up(BME680_SCL_PIN);*/

    //BME680 initialization
    bme.intf = BME68X_I2C_INTF;
    bme.read = bme68x_i2c_read;
    bme.write = bme68x_i2c_write;
    bme.delay_us = bme68x_delay_us;
    bme.intf_ptr = i2c_inst;
    bme.amb_temp = 25;

    int8_t rslt = bme68x_init(&bme);
    if (rslt != BME68X_OK) {
        printf("BME68X initialization failed: %d\n", rslt);
        return false;
    }

    //config sensor settings
    struct bme68x_conf conf;
    conf.filter = BME680_FILTER_SIZE;
    conf.odr = BME68X_ODR_NONE;
    conf.os_hum = BME680_OS_HUMIDITY;
    conf.os_pres = BME680_OS_PRESSURE;
    conf.os_temp = BME680_OS_TEMPERATURE;

    rslt = bme68x_set_conf(&conf, &bme);
    if (rslt != BME68X_OK) {
        printf("Failed to set configuration: %d\n", rslt);
        return false;
    }

    struct bme68x_heatr_conf heatr_conf;
    heatr_conf.enable = BME68X_ENABLE;
    heatr_conf.heatr_temp = 320;
    heatr_conf.heatr_dur = BME680_HEATER_DURATION_MS;
    rslt = bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_conf, &bme);
    if (rslt != BME68X_OK) {
        printf("Failed to set heater configuration: %d\n", rslt);
        return false;
    }

    printf("BME680 initialization complete\n");
    return true;
}

bool bme680_read_data(air_quality_t *data) {
    struct bme68x_data sensor_data;
    uint8_t n_fields;

    int8_t rslt = bme68x_set_op_mode(BME68X_FORCED_MODE, &bme);
    if (rslt != BME68X_OK) return false;

    sleep_ms(BME680_WARMUP_TIME_MS);

    rslt = bme68x_get_data(BME68X_FORCED_MODE, &sensor_data, &n_fields, &bme);
    if (rslt != BME68X_OK) return false;

    if (sensor_data.status & BME68X_NEW_DATA_MSK) {
        data->temperature = sensor_data.temperature;
        data->humidity = sensor_data.humidity;
        data->pressure = sensor_data.pressure;
        data->gas_resistance = sensor_data.gas_resistance / 1000.0f;  //conferts to kilo ohms

        data->voc_ppm = calculate_voc_ppm(
            data->gas_resistance,
            data->temperature,
            data->humidity
        );
        return true;
    }

    return false;
}

float calculate_voc_ppm(float gas_resistance, float temperature, float humidity) {
    float resistance_k = gas_resistance;

    if (resistance_k >= 50.0f) {
        return 0.0f;
    }
    else if (resistance_k >= 10.0f) {
        return (50.0f - resistance_k) * (1.0f / 40.0f);
    }
    else if (resistance_k >= 5.0f) {
        return 1.0f + (10.0f - resistance_k) * (5.0f / 5.0f);
    }
    else if (resistance_k >= 2.0f) {
        return 6.0f + (5.0f - resistance_k) * (4.0f / 3.0f);
    }
    else {
        return 10.0f + (2.0f - resistance_k) * (40.0f / 2.0f);
    }
}