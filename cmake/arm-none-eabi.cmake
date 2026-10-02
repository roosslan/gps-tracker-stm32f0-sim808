set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Если arm-none-eabi-gcc не в PATH, передай -DARM_TOOLCHAIN_DIR=<директория>/bin
set(ARM_TOOLCHAIN_DIR "" CACHE PATH "Директория с arm-none-eabi-gcc")
if(ARM_TOOLCHAIN_DIR)
    set(_prefix "${ARM_TOOLCHAIN_DIR}/arm-none-eabi-")
else()
    set(_prefix "arm-none-eabi-")
endif()
if(CMAKE_HOST_WIN32)
    set(_exe ".exe")
endif()

set(CMAKE_C_COMPILER   "${_prefix}gcc${_exe}")
set(CMAKE_CXX_COMPILER "${_prefix}g++${_exe}")
set(CMAKE_ASM_COMPILER "${_prefix}gcc${_exe}")
set(CMAKE_OBJCOPY      "${_prefix}objcopy${_exe}" CACHE FILEPATH "")
set(CMAKE_SIZE         "${_prefix}size${_exe}" CACHE FILEPATH "")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(_cpu "-mcpu=cortex-m0 -mthumb")
set(_common "${_cpu} -Wall -Wextra -ffunction-sections -fdata-sections")
# Без исключений, RTTI и потокобезопасной инициализации статиков: на 8 КБ RAM
# и без ОС они только тянут в прошивку лишний код из libstdc++.
set(CMAKE_CXX_FLAGS_INIT "${_common} -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "-Og -g3")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-Os -g")
set(CMAKE_C_FLAGS_INIT "${_common} -fno-common")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_cpu} --specs=nano.specs -Wl,--gc-sections -Wl,--no-warn-rwx-segments")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
