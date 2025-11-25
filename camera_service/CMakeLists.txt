# Copyright (c) 2025 UFAL
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_CAMERA_SERVICE)
    set(CAMERA_SERVICE_DIR ${ZEPHYR_CURRENT_MODULE_DIR}/)
    set(INC_DIR ${CAMERA_SERVICE_DIR}/include/)
    set(SRC_DIR ${CAMERA_SERVICE_DIR}/src)
    set(PARENT_SRC_DIR ${CAMERA_SERVICE_DIR}../../zephyr)
    set(CAMERA_SERVICE_PARENT_DIR ${CAMERA_SERVICE_DIR}/../)

    message(WARNING  " Camera service ENABLED. This is for emulation only")
    message(WARNING  " *** Camera service dir: ${CAMERA_SERVICE_DIR}")

    zephyr_include_directories(${PARENT_SRC_DIR}/include/
                               ${INC_DIR}/)

    zephyr_library()
    zephyr_library_sources(${SRC_DIR}/camera_service.c)
endif()
