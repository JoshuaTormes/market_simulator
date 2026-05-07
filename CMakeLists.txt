cmake_minimum_required(VERSION 3.16)
project(simulador LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Build type defaults ────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ── Warning flags ──────────────────────────────────────────────────────────
set(SIM_WARN_FLAGS -Wall -Wextra -Wpedantic)
# -Werror only when explicitly requested (avoids breaking vendored libs)
if(SIM_WERROR)
    list(APPEND SIM_WARN_FLAGS -Werror)
endif()

# ── Sanitizer flags ────────────────────────────────────────────────────────
# Usage: cmake -DSIM_ASAN=ON  or  -DSIM_TSAN=ON
set(SIM_SANITIZER_FLAGS "")
if(SIM_ASAN)
    set(SIM_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
if(SIM_TSAN)
    set(SIM_SANITIZER_FLAGS -fsanitize=thread)
endif()

# ── Third-party UI libs (unchanged from original) ─────────────────────────
find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)

add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/imgui_tables.cpp
)
target_include_directories(imgui PUBLIC external/imgui)
target_compile_options(imgui PRIVATE -w)

add_library(implot STATIC
    external/implot/implot.cpp
    external/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC external/implot external/imgui)
target_link_libraries(implot PUBLIC imgui)
target_compile_options(implot PRIVATE -w)

add_library(imgui_sfml STATIC
    external/imgui-sfml/imgui-SFML.cpp
)
target_include_directories(imgui_sfml PUBLIC external/imgui external/imgui-sfml)
target_link_libraries(imgui_sfml PUBLIC imgui sfml-graphics sfml-window sfml-system)
target_compile_options(imgui_sfml PRIVATE -w)

# ── sim_core: all simulation source (no UI, no main) ──────────────────────
# This is what tests link against.
# Explicitly listed (no glob) so we control exactly which files enter the build.
set(CORE_SRC
    src/core/Clock.cpp
    src/core/Config.cpp
    src/core/Logger.cpp
    src/core/RngService.cpp
    # Phase 1
    src/orderbook/OrderBookV2.cpp
    src/matching/MatchingEngine.cpp
    # Phase 2
    src/ledger/PositionLedger.cpp
    src/clearing/Clearing.cpp
    # Phase 3
    src/marketdata/MarketDataPublisher.cpp
    # Phase 4
    src/economics/GBMProcess.cpp
    src/economics/JumpDiffusionProcess.cpp
    src/economics/OUProcess.cpp
    src/economics/RegimeSwitchingProcess.cpp
    src/economics/PoissonNewsProcess.cpp
    # Phase 5
    src/agents/AgentBase.cpp
    src/agents/MarketMakerAS.cpp
    src/agents/InformedTraderKyle.cpp
    src/agents/NoiseTrader.cpp
    src/agents/MomentumTrader.cpp
    src/agents/MeanReverterOU.cpp
    src/agents/ValueInvestor.cpp
    src/agents/InstitutionalExecutor.cpp
    src/agents/StopLossCluster.cpp
    src/agents/NewsReactor.cpp
    src/agents/AgentFactory.cpp
    src/agents/AgentRunner.cpp
    src/risk/RiskGate.cpp
    # Phase 6
    src/marketdata/SnapshotBuffer.cpp
    src/sim/SimulationLoop.cpp
    # Phase 7+: append here
)

add_library(sim_core STATIC ${CORE_SRC})
target_include_directories(sim_core PUBLIC src)
target_compile_options(sim_core PRIVATE
    ${SIM_WARN_FLAGS}
    ${SIM_SANITIZER_FLAGS}
)
target_link_options(sim_core INTERFACE ${SIM_SANITIZER_FLAGS})

# ── simulador executable ───────────────────────────────────────────────────
add_executable(simulador
    src/main.cpp
    src/visual/VisualApp.cpp
)
target_include_directories(simulador PRIVATE src)
target_link_libraries(simulador PRIVATE
    sim_core
    imgui
    implot
    imgui_sfml
    sfml-graphics
    sfml-window
    sfml-system
)
target_compile_options(simulador PRIVATE
    ${SIM_WARN_FLAGS}
    ${SIM_SANITIZER_FLAGS}
)
target_link_options(simulador PRIVATE ${SIM_SANITIZER_FLAGS})

if(APPLE)
    target_link_libraries(simulador PRIVATE "-framework OpenGL" "-framework Cocoa")
endif()

# ── Tests ─────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
