cmake_minimum_required(VERSION 3.16)

# CLI: analysis_stylized_facts <log.bin> [report.csv]
add_executable(analysis_stylized_facts StylizedFacts.cpp)
target_include_directories(analysis_stylized_facts PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(analysis_stylized_facts PRIVATE sim_core)
target_compile_options(analysis_stylized_facts PRIVATE ${SIM_WARN_FLAGS})
