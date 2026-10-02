cmake_minimum_required(VERSION 3.22)

# Один CMakeLists на два режима:
#  - с toolchain-файлом cmake/arm-none-eabi.cmake собирается прошивка для STM32F051R8;
#  - без него собираются и запускаются тесты чистой логики на ПК.
project(gps_tracker CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LOGIC_SOURCES src/text.cpp src/gps.cpp src/sms.cpp src/report.cpp src/rtt.cpp)

# В CI включается -DWARNINGS_AS_ERRORS=ON: любое предупреждение компилятора — ошибка сборки.
option(WARNINGS_AS_ERRORS "Считать предупреждения компилятора ошибками" OFF)

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()

    # logic_tests — чистая логика; at_tests — AT-движок и команды SIM808
    # с поддельным модулем вместо UART (tests/fake_hw.cpp).
    add_executable(logic_tests tests/test_main.cpp ${LOGIC_SOURCES})
    add_executable(at_tests tests/test_at.cpp tests/fake_hw.cpp src/at.cpp src/sim808.cpp ${LOGIC_SOURCES})
    foreach(t logic_tests at_tests)
        target_include_directories(${t} PRIVATE src tests)
        if(MSVC)
            target_compile_options(${t} PRIVATE /W4 /utf-8 $<$<BOOL:${WARNINGS_AS_ERRORS}>:/WX>)
        else()
            target_compile_options(${t} PRIVATE -Wall -Wextra $<$<BOOL:${WARNINGS_AS_ERRORS}>:-Werror>)
        endif()
        add_test(NAME ${t} COMMAND ${t})
    endforeach()
    return()
endif()

if(WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
endif()

# --- Прошивка -------------------------------------------------------------

include(FetchContent)
# SOURCE_SUBDIR указывает в несуществующую директорию: нам нужны только заголовки,
# их собственные CMakeLists подключать не надо.
FetchContent_Declare(cmsis_core
    GIT_REPOSITORY https://github.com/ARM-software/CMSIS_6.git
    GIT_TAG        v6.3.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake)
FetchContent_Declare(cmsis_device_f0
    GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis-device-f0.git
    GIT_TAG        v2.3.7
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake)
FetchContent_MakeAvailable(cmsis_core cmsis_device_f0)

add_executable(gps_tracker
    src/startup.cpp
    src/syscalls.cpp
    src/board.cpp
    src/uart.cpp
    src/at.cpp
    src/sim808.cpp
    src/main.cpp
    ${LOGIC_SOURCES})
set_target_properties(gps_tracker PROPERTIES SUFFIX .elf)

# Белый список номеров (src/allowed_numbers.h, не в git) проверяется при каждой сборке.
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
set(ALLOWED_NUMBERS_BUILD_H ${GENERATED_DIR}/allowed_numbers_build.h)
add_custom_target(allowed_numbers
    COMMAND ${CMAKE_COMMAND}
            -DSRC=${CMAKE_SOURCE_DIR}/src/allowed_numbers.h
            -DOUT=${ALLOWED_NUMBERS_BUILD_H}
            -P ${CMAKE_SOURCE_DIR}/cmake/gen_allowed_numbers.cmake
    BYPRODUCTS ${ALLOWED_NUMBERS_BUILD_H}
    VERBATIM)
add_dependencies(gps_tracker allowed_numbers)

target_include_directories(gps_tracker PRIVATE
    ${GENERATED_DIR}
    src
    ${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include
    ${cmsis_device_f0_SOURCE_DIR}/Include)
target_compile_definitions(gps_tracker PRIVATE STM32F051x8)

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/linker/STM32F051R8Tx.ld)
target_link_options(gps_tracker PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,-Map=$<TARGET_FILE_DIR:gps_tracker>/gps_tracker.map
    -Wl,--print-memory-usage)
set_target_properties(gps_tracker PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

add_custom_command(TARGET gps_tracker POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:gps_tracker> $<TARGET_FILE_DIR:gps_tracker>/gps_tracker.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:gps_tracker> $<TARGET_FILE_DIR:gps_tracker>/gps_tracker.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:gps_tracker>
    VERBATIM)

# cmake --build --preset firmware --target flash
find_program(STM32_PROGRAMMER_CLI STM32_Programmer_CLI
    PATHS "C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin")
find_program(OPENOCD openocd)
if(STM32_PROGRAMMER_CLI)
    add_custom_target(flash
        COMMAND ${STM32_PROGRAMMER_CLI} -c port=SWD -w $<TARGET_FILE:gps_tracker> -v -rst
        DEPENDS gps_tracker
        USES_TERMINAL)
elseif(OPENOCD)
    add_custom_target(flash
        COMMAND ${OPENOCD} -f board/stm32f0discovery.cfg
                -c "program $<TARGET_FILE:gps_tracker> verify reset exit"
        DEPENDS gps_tracker
        USES_TERMINAL)
endif()

# cmake --build --preset firmware --target log — RTT-лог через ST-LINK (Ctrl+C — выход)
find_package(Python3 COMPONENTS Interpreter)
if(OPENOCD AND Python3_Interpreter_FOUND)
    add_custom_target(log
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/rtt_log.py --openocd ${OPENOCD}
        USES_TERMINAL)
endif()
