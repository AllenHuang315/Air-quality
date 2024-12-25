#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "config/pin_config.h"
#include "lis3.h"

const float NO_MOVEMENT_THRESH = 0.10f;
const uint32_t NO_MOVEMENT_DURATION_MS = 100000000;  // 10000 s in milliseconds// 1-second check interval
static uint32_t no_movement_start_time;
static bool no_movement_timer_running = false;
//const float ACCEL_GRAV = 9.81f;

static i2c_inst_t *i2c_port;

// operation modes:
// 0 - low power mode (1 ms turn on time)
// 1 - normal mode (1.6 ms turn on time)
// (unused) 2 - high resolution mode (7/ODR ms turn on time)
//void LIS3_init(i2c_inst_t *i2c_inst, int operation_mode)
void LIS3_init(i2c_inst_t *i2c_inst, int operation_mode) {
	printf("Starting LIS3DH initialization...\n");
	i2c_port = i2c_inst;

	// i2c initialization
	//i2c_init(i2c_port, LIS3_I2C_FREQ);
	/*gpio_set_function(LIS3_SDA_PIN, GPIO_FUNC_I2C);
	gpio_set_function(LIS3_SCL_PIN, GPIO_FUNC_I2C);
	gpio_pull_up(LIS3_SDA_PIN);
	gpio_pull_up(LIS3_SCL_PIN);*/
	// make the I2C pins available to picotool
	//bi_decl(bi_2pins_with_func(LIS3_SDA_PIN, LIS3_SCL_PIN, GPIO_FUNC_I2C));

	uint8_t buf[2];
	// turn low power mode and 400 Hz on
	if (operation_mode == 0) {
		buf[0] = CTRL_REG1;
		buf[1] = 0x7F;
		i2c_write_blocking(i2c_port, LIS3_I2C_ADDR, buf, 2, false);

		// turn normal mode and 1.314 kHz on
	} else if (operation_mode == 1) {
		buf[0] = CTRL_REG1;
		buf[1] = 0x97;
		i2c_write_blocking(i2c_port, LIS3_I2C_ADDR, buf, 2, false);
	}

	// high pass filter initialize - filters out gravity consideration
	buf[0] = CTRL_REG2;
	buf[1] = 0x09;
	i2c_write_blocking(i2c_port, LIS3_I2C_ADDR, buf, 2, false);

	// latching interrupt
	//buf[0] = CTRL_REG5;
	//buf[1] = 0x08;
	//i2c_write_blocking(i2c_port, LIS3_I2C_ADDR, buf, 2, false);

	// set up interrupt configs
	// set interrupt threshold to 250 mg
	buf[0] = INT1_THS;
	buf[1] = 0x10;
	i2c_write_blocking(i2c_port, LIS3_I2C_ADDR, buf, 2, false);

	// default INT1_DURATION set to 0
	// set interrupt to generate when either X, Y or Z axis is high
	buf[0] = INT1_CFG;
	buf[1] = 0x2A;
	i2c_write_blocking(i2c_port, LIS3_I2C_ADDR, buf, 2, false);

	enable_movement_detection();
}

// sleep to wake interrupt setup
void enable_movement_detection() {
	uint8_t buf[2];

	// interrupt initialization
	// interrupt activity 1 driven to INT1 pad
	buf[0] = CTRL_REG3;
	buf[1] = 0x40;
	i2c_write_blocking(i2c_port, LIS3_I2C_ADDR, buf, 2, false);
}

void disable_movement_detection() { // disable interrupt
	uint8_t buf[2];

	// disable interrupt
	buf[0] = CTRL_REG3;
	buf[1] = 0x0;
	i2c_write_blocking(i2c_port, LIS3_I2C_ADDR, buf, 2, false);
}

void LIS3_clear_interrupt() {
	uint8_t int1_src = INT1_SRC;
	uint8_t temp;
	i2c_write_blocking(i2c_default, LIS3_I2C_ADDR, &int1_src, 1, true);
	i2c_read_blocking(i2c_default, LIS3_I2C_ADDR, &temp, 1, false);
}

// reg is address of lsb
float LIS3_read_axis(uint8_t reg_l, uint8_t reg_h) {
	// read two bytes of data and store in a 16 bit data structure
	uint8_t lsb;
	uint8_t msb;
	i2c_write_blocking(i2c_default, LIS3_I2C_ADDR, &reg_l, 1, true);
	i2c_read_blocking(i2c_default, LIS3_I2C_ADDR, &lsb, 1, false);

	i2c_write_blocking(i2c_default, LIS3_I2C_ADDR, &reg_h, 1, true);
	i2c_read_blocking(i2c_default, LIS3_I2C_ADDR, &msb, 1, false);

	uint16_t raw_val = (msb << 8) | lsb;

	//convert to meaningful acceleration values
	float sensitivity = 0.004f; // g per unit
	float scaling = 64 / sensitivity;

	//printf("HEX %x\n", raw_val);
	//printf("UNSIGNED %u\n", raw_val);
	//printf("SIGNED %i\n", (int16_t)raw_val);
	//printf("SCALED %f\n", (float) ((int16_t) raw_val) / scaling);

	return (float) ((int16_t) raw_val) / scaling;
}

lis3_data_t LIS3_read_data() { // filtering out acceleration due to gravity so check that magnitude is close to 0
  	lis3_data_t data;
	data.x = LIS3_read_axis(OUT_X_L, OUT_X_H);
    data.y = LIS3_read_axis(OUT_Y_L, OUT_Y_H);
    data.z = LIS3_read_axis(OUT_Z_L, OUT_Z_H);
    return data;
}

// perform vector difference to determine movement
bool LIS3_is_moving() {
	lis3_data_t curr_data = LIS3_read_data();

    double magnitude = sqrt(pow(curr_data.x, 2) + pow(curr_data.y, 2) + pow(curr_data.z, 2));
	if (magnitude > NO_MOVEMENT_THRESH) {
		return true;
	}
    return false;
}

// Non-blocking function to check for no movement over a 10-minute period
bool check_no_movement_for_duration() {

	// Check if there's movement
	if (LIS3_is_moving()) {
		printf("Movement detected, resetting timer.\n");
		no_movement_timer_running = false;  // stop timer
		return false;  // return false as movement was detected
	}

	//printf("No movement detected.\n");
	if (!no_movement_timer_running) {
		no_movement_start_time = to_ms_since_boot(get_absolute_time()); // gets number of ms since boot
		no_movement_timer_running = true;
		return false; // timer just started running
	}

	// Calculate elapsed time
	uint32_t current_time = to_ms_since_boot(get_absolute_time());
	uint32_t elapsed_time = current_time - no_movement_start_time;

	// If 10 minutes have passed without movement, return true
	if (elapsed_time >= NO_MOVEMENT_DURATION_MS) {
		no_movement_timer_running = false;  // Stop the timer after detecting no movement
		return true;  // No movement detected for 10 minutes
	}

	return false;  // No conclusion yet; keep checking
}