#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

typedef struct __attribute__((packed)) {
    float temperature;
    float humidity;
    float pressure;
    float gas_resistance;
    float voc_ppm;
    float pm25;
} sensor_data;

int start_ble_service(void);
void update_sensor_data(sensor_data* data);
void send_sensor_data(void);
void stop_ble_service(void);


#endif // BLE_SERVICE_H