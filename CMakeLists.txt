# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.13)

# Bluepad32 is a git submodule. The ESP-IDF component lives one level down.
set(BLUEPAD32_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/components/bluepad32)
set(EXTRA_COMPONENT_DIRS ${BLUEPAD32_ROOT}/src/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(nutcracker_idf)
