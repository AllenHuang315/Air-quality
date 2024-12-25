# AirSense: A Wearable Air Quality Monitoring Devic

# ABSTRACT
 The project is an IoT system using wearable devices as backpack
 clips to collect localized air and environmental data as users
 move. The edge device includes four key sensors: PM2.5,
 temperature/humidity/VOC, and an accelerometer. The Raspberry
 Pi Pico W will retrieve GPS data from the user's phone, helping
 distinguish accurate location. The accelerometer will monitor
 movement, and if the device is stationary—such as being left on a
 surface—this indicates inactivity, triggering the system to enter a
 low-power sleep mode by reducing the clock frequency and
 turning off the PM2.5 sensor to conserve battery. This is
 particularly important as our design is a wearable clip, ensuring
 portability and efficient power usage. All collected sensor data
 will be transmitted via BLE to the user’s phone and then uploaded
 to the cloud. The user’s phone will also further process the data
 and display, offering users a comprehensive overview of air
 quality and environmental conditions in real-time and the forecast.

# 1. INTRODUCTION
 Air quality is a critical environmental metric that has a direct
 impact on human health and quality of life. Living or spending
 extended amounts of time in areas with poor air quality can lead
 to various issues such as poor respiratory function, allergies, and
 other long-term health problems such as increased risk of
 respiratory system-related cancers. Traditionally, air quality
 monitoring stations are stationary and provide limited spatial
 resolution. This leads to often misleading or inaccurate
 information as air quality can vary greatly in urban environments.
 For example, areas by busy roads or freeways are much more
 likely to have poor air quality as opposed to an area by a large
 park. Stationary air quality monitoring stations also do not provide
 any information about air quality inside of buildings.
![Assembled device](https://github.com/user-attachments/assets/b90401e6-d13d-4460-95de-060430609c51)
