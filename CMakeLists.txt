cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(base_sample)

# Diretório de headers
zephyr_include_directories(include)

# Arquivos .c
target_sources(app PRIVATE
    src/main.c
    src/display.c
    src/radar.c
)