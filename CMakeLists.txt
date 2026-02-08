# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for
# the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
  set(USERHOME $ENV{USERPROFILE})
else()
  set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if(EXISTS ${picoVscode})
  include(${picoVscode})
endif()
# ==============================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)
# FreeRTOS removed - using bare-metal super loop architecture

project(BasicRTOS C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Create httpd content library first
pico_add_library(pico_httpd_content NOFLAG)
pico_set_lwip_httpd_content(pico_httpd_content INTERFACE
      ${CMAKE_CURRENT_LIST_DIR}/content/welcome.shtml
      ${CMAKE_CURRENT_LIST_DIR}/content/dashboard.html
      ${CMAKE_CURRENT_LIST_DIR}/content/zones.shtml
      ${CMAKE_CURRENT_LIST_DIR}/content/schedules.shtml
      ${CMAKE_CURRENT_LIST_DIR}/content/404.html
      ${CMAKE_CURRENT_LIST_DIR}/content/api/sensors.json
      ${CMAKE_CURRENT_LIST_DIR}/content/api/zones.json
      ${CMAKE_CURRENT_LIST_DIR}/content/api/schedules.json
      ${CMAKE_CURRENT_LIST_DIR}/content/api/zone_status.json
      ${CMAKE_CURRENT_LIST_DIR}/content/api/success.json
      ${CMAKE_CURRENT_LIST_DIR}/content/api/error.json
      ${CMAKE_CURRENT_LIST_DIR}/content/api/time.json
      )

# Add executable. Default name is the project name, version 0.1

add_executable(BasicRTOS
      BasicRTOS.c
      src/network.c
      src/dht22.c
      src/config_flash.c
      src/zones.c
      src/scheduler.c
      src/lcd_display.c
      src/fault_tolerance.c
      lcd.c)


pico_set_program_name(BasicRTOS "BasicRTOS")
pico_set_program_version(BasicRTOS "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(BasicRTOS 0)
pico_enable_stdio_usb(BasicRTOS 1)

# Add the standard library to the build
target_link_libraries(BasicRTOS PRIVATE
        pico_multicore
        pico_stdlib
        hardware_i2c
        hardware_watchdog
        pico_flash
        pico_cyw43_arch_lwip_poll
        pico_lwip_http
        pico_lwip_mdns
        pico_lwip_sntp
        pico_httpd_content)

# Add the standard include files to the build
target_include_directories(BasicRTOS PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        include/
)

# Add any user requested libraries

pico_add_extra_outputs(BasicRTOS)

