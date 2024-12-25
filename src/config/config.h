#ifndef CONFIG_H
#define CONFIG_H

#include "pin_config.h"

//General timing configs
#define MAIN_LOOP_DELAY_MS        1000
#define SERIAL_INIT_DELAY_MS      6000

//I2C configs
#define I2C0_FREQ         400000  //400 khz

//BME680 configs
#define BME680_SAMPLE_PERIOD_MS     3000
#define BME680_HEATER_DURATION_MS   150
#define BME680_WARMUP_TIME_MS       250
#define BME680_VOC_MAX_PPM          10.0f

#endif //CONFIG_H
