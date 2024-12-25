cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "D:/0_UW/class/2024_fall/EE542/project/pico-sdk")
set(PICO_EXTRAS_PATH "D:/0_UW/class/2024_fall/EE542/project/pico-extras")
set(PICO_BOARD pico_w)
include(pico_sdk_import.cmake)
include(${PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)

project(air_quality_monitor C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


#commented out as not necessary, but if issues with cmake finding pico sdk could be useful


#set(PICOTOOL_FETCH_FROM_GIT_PATH "D:/0_UW/class/2024_fall/EE542/project/pico-tools")


pico_sdk_init()

#add_library(pico_sleep STATIC
#    ${PICO_EXTRAS_PATH}/src/rp2_common/pico_sleep/sleep.c
#)

# 添加 GATT 生成命令
add_custom_command(
    OUTPUT  ${CMAKE_SOURCE_DIR}/src/ble/gatt.h
    COMMAND python ${PICO_SDK_PATH}/lib/btstack/tool/compile_gatt.py 
            ${CMAKE_SOURCE_DIR}/src/ble/gatt_streamer_server.gatt 
            ${CMAKE_SOURCE_DIR}/src/ble/gatt.h
    DEPENDS ${CMAKE_SOURCE_DIR}/src/ble/gatt_streamer_server.gatt
)



add_executable(${PROJECT_NAME}
        src/main.c
        src/sensors/bme680.c
        src/sensors/sensorutils/bme68x/bme68x.c
	src/sensors/pmsa003.c
	src/ble/ble_service.c
	src/ble/gatt.h
	src/sensors/lis3.c

)

target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/sensors
        ${CMAKE_SOURCE_DIR}/src/sensors/sensorutils/bme68x
	${CMAKE_SOURCE_DIR}/src/ble
	${PICO_SDK_PATH}/src
	${PICO_SDK_PATH}/lib/btstack/src
	${PICO_SDK_PATH}/lib/btstack/port/renesas-tb-s1ja-cc256x/template/btstack_example/src
	${PICO_SDK_PATH}/lib/btstack/3rd-party/bluedroid/encoder/include
	${PICO_SDK_PATH}/lib/btstack/3rd-party/bluedroid/decoder/include
	${PICO_SDK_PATH}/lib/lwip/src/include
	${PICO_SDK_PATH}/lib/lwip/contrib/examples/example_app
	${PICO_SDK_PATH}/lib/lwip/contrib/ports/win32/include
	${PICO_SDK_PATH}/src/rp2_common/hardware_adc/include
	${PICO_SDK_PATH}/src/rp2_common/hardware_rtc/include/hardware
	${PICO_SDK_PATH}/src/rp2040/hardware_structs/include
	${PICO_SDK_PATH}/src/rp2_common/pico_runtime/include
	${PICO_EXTRAS_PATH}/src/rp2_common/pico_sleep/include
	#${PICO_EXTRAS_PATH}/src/rp2_common/pico_sleep/include/pico
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    HCI_OUTGOING_PRE_BUFFER_SIZE=4
    HCI_ACL_CHUNK_SIZE_ALIGNMENT=4
    HAVE_MALLOC
)

target_link_libraries(${PROJECT_NAME}
        pico_stdlib
	#pico_sleep
	#hardware_clocks
	hardware_sleep
	hardware_rosc
	hardware_rtc
        hardware_i2c
	pico_btstack_ble
	pico_btstack_cyw43
        pico_cyw43_arch_none	
        m
	hardware_adc
	#${PICO_EXTRAS_PATH}/src/rp2_common/pico_sleep    # 修改這行
)


pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})
#example_auto_set_url(${PROJECT_NAME})