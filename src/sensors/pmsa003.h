#ifndef PMSA003_H
#define PMSA003_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

#define I2C_PORT i2c1
#define PMSA003I_I2C_ADDR 0x12
#define SDA_PIN 14  // GPIO 14 (Pin 19)
#define SCL_PIN 15  // GPIO 15 (Pin 20)

typedef struct {
    uint16_t pm1_0_standard;
    uint16_t pm2_5_standard;
    uint16_t pm10_standard;
    uint16_t pm1_0_env;
    uint16_t pm2_5_env;
    uint16_t pm10_env;
} pmsa003_data_t;

void pmsa003_init(i2c_inst_t *i2c);
bool pmsa003_read_data(pmsa003_data_t *data);

#endif //PMSA003_H