cmake_minimum_required(VERSION 3.15)
project(EdgeFirmwareMock C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(
    edge/app/include
    edge/osal/include
    edge/hal/include
    edge/middleware/serializer
)

add_executable(test_edge_mock
    edge/tests/unit/test_edge_mock.c
    edge/app/src/app_state_machine.c
    edge/middleware/serializer/telemetry_serializer.c
    edge/osal/mock/osal_mock.c
    edge/hal/mock/hal_mock.c
)

if (WIN32)
    target_link_libraries(test_edge_mock)
else()
    target_link_libraries(test_edge_mock pthread)
endif()
