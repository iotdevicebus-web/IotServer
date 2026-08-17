cmake_minimum_required(VERSION 3.15)
project(STM32_Edge_Firmware C ASM)

set(CMAKE_C_STANDARD 99)

include_directories(
    ../../app/include
    ../../osal/include
    ../../hal/include
    ../../middleware/serializer
    ../../middleware/telemetry
)

add_library(stm32_edge_lib STATIC
    ../../app/src/app_state_machine.c
    ../../middleware/serializer/telemetry_serializer.c
    ../../middleware/serializer/protobuf_serializer.c
    ../../middleware/telemetry/telemetry_buffer.c
    hal_stm32_platform.c
)



target_compile_definitions(stm32_edge_lib PRIVATE STM32 __arm__)
