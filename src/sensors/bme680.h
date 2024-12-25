#ifndef BME680_H
#define BME680_H

#include "hardware/i2c.h"
#include "sensorutils/bme68x/bme68x.h"
#include <stdbool.h>

#define BME680_I2C_ADDR         0x77
#define BME680_I2C_FREQ         400000  //400 khz

//BME680 sensor settings
#define BME680_OS_HUMIDITY         BME68X_OS_16X         //humidity oversampling
#define BME680_OS_PRESSURE         BME68X_OS_1X          //pressure oversampling
#define BME680_OS_TEMPERATURE      BME68X_OS_2X          //temperature oversampling
#define BME680_FILTER_SIZE         BME68X_FILTER_SIZE_3  //IIR filter size

typedef struct {
    float temperature;
    float humidity;
    float pressure;
    float gas_resistance;
    float voc_ppm;
} air_quality_t;


float calculate_voc_ppm(float gas_resistance, float temperature, float humidity);

bool bme680_init(i2c_inst_t *i2c_inst);
bool bme680_read_data(air_quality_t *data);

#endif