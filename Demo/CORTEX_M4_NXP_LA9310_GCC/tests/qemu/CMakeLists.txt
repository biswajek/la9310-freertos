# QEMU inter-task IPC test build
# Target: QEMU mps2-an386 (ARM Cortex-M4)

INCLUDE(CMakeForceCompiler)
SET(CMAKE_SYSTEM_NAME Generic)
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

ENABLE_LANGUAGE(ASM)
SET(CMAKE_STATIC_LIBRARY_PREFIX)
SET(CMAKE_STATIC_LIBRARY_SUFFIX)
SET(CMAKE_EXECUTABLE_LIBRARY_PREFIX)
SET(CMAKE_EXECUTABLE_LIBRARY_SUFFIX)

# Paths
SET(TestDir    ${CMAKE_CURRENT_SOURCE_DIR})
SET(DemoDir    ${TestDir}/../..)
SET(LA9310Root ${DemoDir}/../..)
SET(PAL_DIR    ${LA9310Root}/../../Platform/PAL/install/pal/freertos-cm4)

# Toolchain flags
SET(CPU_FLAGS "-mcpu=cortex-m4 -mfloat-abi=soft -mthumb")

SET(CMAKE_ASM_FLAGS
    "${CPU_FLAGS} -g -Wall -fno-common -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -Os -mapcs")

SET(CMAKE_C_FLAGS
    "${CPU_FLAGS} -g -fno-strict-aliasing -MMD -MP -Wall -fno-common -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -Os -mapcs -std=gnu99 -Wno-address-of-packed-member -D__NDEBUG")

# Linker: semihosting via rdimon, no nano specs conflict
SET(CMAKE_EXE_LINKER_FLAGS
    "${CPU_FLAGS} --specs=rdimon.specs -lrdimon -lm -lc -lgcc -Wall -fno-common -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -Os -mthumb -mapcs -Xlinker --gc-sections -Xlinker -static -T${TestDir}/mps2_an386.ld -Xlinker -Map=${CMAKE_BINARY_DIR}/ipc_test.map")

# Include paths
INCLUDE_DIRECTORIES(
    ${TestDir}                                              # FreeRTOSConfig.h
    ${DemoDir}/../../Source/portable/GCC/ARM_CM4
    ${DemoDir}/../../Source/include
    ${LA9310Root}/common_headers
    ${DemoDir}/include
    ${DemoDir}/platform/ARM_CM4/include
    ${DemoDir}/platform/boards/la9310
    ${DemoDir}/drivers/phy_timer
    ${PAL_DIR}/include
)

# Sources — only the messaging layer + FreeRTOS kernel + test files
SET(TEST_SOURCES
    # Test startup + cases
    ${TestDir}/startup_qemu.S
    ${TestDir}/test_main.c
    ${TestDir}/test_ipc.c
    ${TestDir}/stubs.c

    # FreeRTOS kernel
    ${DemoDir}/../../Source/portable/GCC/ARM_CM4/port.c
    ${DemoDir}/../../Source/portable/MemMang/heap_2.c
    ${DemoDir}/../../Source/croutine.c
    ${DemoDir}/../../Source/event_groups.c
    ${DemoDir}/../../Source/list.c
    ${DemoDir}/../../Source/queue.c
    ${DemoDir}/../../Source/tasks.c
    ${DemoDir}/../../Source/timers.c

    # IPC / messaging layer under test
    ${DemoDir}/ipiQueue.c
    ${DemoDir}/ms_procx_comm.c
    ${DemoDir}/ms_l1_controller_fwk.c
    ${DemoDir}/ms_globals.c
    ${DemoDir}/ms_logger.c
    ${DemoDir}/freertos_compat.c
)

ADD_EXECUTABLE(ipc_test ${TEST_SOURCES})
SET_TARGET_PROPERTIES(ipc_test PROPERTIES OUTPUT_NAME "ipc_test.elf")

TARGET_LINK_LIBRARIES(ipc_test -Wl,--start-group)
TARGET_LINK_LIBRARIES(ipc_test ${PAL_DIR}/lib/libPAL.a)
TARGET_LINK_LIBRARIES(ipc_test -Wl,--end-group)

ADD_CUSTOM_COMMAND(TARGET ipc_test POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -Obinary
        ${EXECUTABLE_OUTPUT_PATH}/ipc_test.elf
        ${EXECUTABLE_OUTPUT_PATH}/ipc_test.bin)
