# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(SWGenerator_code C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(encoder build.rotary_encoder)
# Add executable. Default name is the project name, version 0.1

add_executable(SWGenerator_code main.c ssd1306.c ssd1306_setup.c core1_entry.c AT24C256.c)

pico_set_program_name(SWGenerator_code "SWGenerator_code")
pico_set_program_version(SWGenerator_code "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(SWGenerator_code 0)
pico_enable_stdio_usb(SWGenerator_code 1)
# Add the standard library to the build
target_link_libraries(SWGenerator_code
    pico_stdlib
    hardware_i2c
    hardware_uart
    pico_multicore
    rp2040_rotary_encoder
    
)

# Add the standard include files to the build
target_include_directories(SWGenerator_code PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(SWGenerator_code 
        hardware_i2c
        hardware_pwm
        )

pico_add_extra_outputs(SWGenerator_code)

