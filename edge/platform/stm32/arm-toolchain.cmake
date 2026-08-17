set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# STM32CubeCLT の GCC パス
set(TOOLCHAIN_PREFIX "C:/ST/STM32CubeCLT_1.19.0/GNU-tools-for-STM32/bin/arm-none-eabi-")

set(CMAKE_C_COMPILER "${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++.exe")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_OBJCOPY "${TOOLCHAIN_PREFIX}objcopy.exe")
set(CMAKE_SIZE "${TOOLCHAIN_PREFIX}size.exe")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Cortex-M4 / STM32F4 / L4 フラグ
set(ARM_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS "${ARM_FLAGS} -fdata-sections -ffunction-sections -Wall -O2" CACHE INTERNAL "C Compiler options")
set(CMAKE_EXE_LINKER_FLAGS "${ARM_FLAGS} -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs" CACHE INTERNAL "Linker options")
