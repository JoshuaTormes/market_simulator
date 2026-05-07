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

add_sim_test(test_rng            unit/test_rng.cpp)
add_sim_test(test_clock          unit/test_clock.cpp)
add_sim_test(test_rolling_window unit/test_rolling_window.cpp)
add_sim_test(test_eventbus       unit/test_eventbus.cpp)
# Phase 1
add_sim_test(test_orderbook      unit/test_orderbook.cpp)
add_sim_test(test_matching       unit/test_matching.cpp)
# Phase 2
add_sim_test(test_ledger         unit/test_ledger.cpp)
add_sim_test(test_clearing       unit/test_clearing.cpp)
# Phase 3
add_sim_test(test_market_data_publisher  unit/test_market_data_publisher.cpp)
add_sim_test(test_agent_snapshot_noise   unit/test_agent_snapshot_noise.cpp)
# Phase 4
add_sim_test(test_fundamental_processes  unit/test_fundamental_processes.cpp)
add_sim_test(test_news_process           unit/test_news_process.cpp)
# Phase 5
add_sim_test(test_market_maker_as        unit/test_market_maker_as.cpp)
add_sim_test(test_kyle_informed          unit/test_kyle_informed.cpp)
add_sim_test(test_noise_trader           unit/test_noise_trader.cpp)
add_sim_test(test_risk_gate              unit/test_risk_gate.cpp)
add_sim_test(test_agent_factory          unit/test_agent_factory.cpp)
# Phase 6
add_sim_test(test_snapshot_buffer        unit/test_snapshot_buffer.cpp)
add_sim_test(test_sim_loop_smoke         integration/test_sim_loop_smoke.cpp)
