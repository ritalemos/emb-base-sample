# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)


# Adiciona o diretório camera_service como módulo extra do Zephyr
set(ZEPHYR_EXTRA_MODULES
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_service
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(emb_base_sample)

# Diretório de headers
zephyr_include_directories(include)

# Arquivos .c
target_sources(app PRIVATE
    src/main.c
    src/util/display.c
    src/util/radar.c
    src/util/mercosul_plate.c
)

