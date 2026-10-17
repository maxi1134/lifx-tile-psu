cmake_minimum_required(VERSION 3.16.0)
# Only a stub app: we only want bootloader.bin. Fewer components also keeps the
# link command under the Windows command-line length limit.
set(COMPONENTS src __pio_env)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bootloader_xmc_a)
