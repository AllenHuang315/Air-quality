#ifndef LIS3_H
#define LIS3_H

#include <stdint.h>
#include <string.h>

#define LIS3_I2C_ADDR 0x18
#define LIS3_I2C_FREQ 400000  //400 khz

#define CTRL_REG1 0x20
#define CTRL_REG2 0x21
#define CTRL_REG3 0x22
#define CTRL_REG4 0x23
#define CTRL_REG5 0x24

#define INT1_CFG 0x30
#define INT1_THS 0x32
#define INT2_THS 0x36
#define INT1_SRC 0x31

#define OUT_X_L 0x28
#define OUT_X_H 0x29
#define OUT_Y_L 0x2A
#define OUT_Y_H 0x2B
#define OUT_Z_L 0x2C
#define OUT_Z_H 0x2D

typedef struct
{
    float x;
    float y;
    float z;
} lis3_data_t;

// initialize LIS3DH sensor
void LIS3_init(i2c_inst_t *i2c_inst, int operation_mode);
//void LIS3_init(int operation_mode);

void enable_movement_detection();

void disable_movement_detection();

float LIS3_read_axis(uint8_t reg_l, uint8_t reg_h);

lis3_data_t LIS3_read_data();

bool LIS3_is_moving();

bool check_no_movement_for_duration();

#endif //LIS3_H