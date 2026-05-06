cmake_minimum_required(VERSION 3.16)

# Catch2 v3 amalgamated — single compilation unit
add_library(catch2_impl STATIC
    ${CMAKE_SOURCE_DIR}/external/catch2/catch_amalgamated.cpp
)
target_include_directories(catch2_impl PUBLIC
    ${CMAKE_SOURCE_DIR}/external/catch2
)
target_compile_options(catch2_impl PRIVATE
    -w  # suppress warnings in third-party code
)

# Helper to register a unit test executable
function(add_sim_test name src)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/external/catch2
    )
    target_link_libraries(${name} PRIVATE catch2_impl sim_core)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_sim_test(test_rng         unit/test_rng.cpp)
add_sim_test(test_clock       unit/test_clock.cpp)
add_sim_test(test_rolling_window unit/test_rolling_window.cpp)
add_sim_test(test_eventbus    unit/test_eventbus.cpp)
