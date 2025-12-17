cmake_minimum_required(VERSION 3.20)
project(simulador_mercado)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(simulador
    src/main.cpp
    src/simulator.cpp
)
