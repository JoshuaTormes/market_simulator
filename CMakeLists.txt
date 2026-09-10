cmake_minimum_required(VERSION 3.16)

# ── Toolchain defaults (must come before project()) ────────────────────────
# On macOS, default to the Apple clang toolchain. A Homebrew GCC picked up by
# accident links against an SDK that may not exist ("ld: library 'System' not
# found"). An explicit -DCMAKE_CXX_COMPILER=... on the command line still wins.
if(APPLE)
    if(NOT DEFINED CMAKE_C_COMPILER AND EXISTS /usr/bin/clang)
        set(CMAKE_C_COMPILER /usr/bin/clang)
    endif()
    if(NOT DEFINED CMAKE_CXX_COMPILER AND EXISTS /usr/bin/clang++)
        set(CMAKE_CXX_COMPILER /usr/bin/clang++)
    endif()
endif()

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

# ── Third-party UI libs (optional) ────────────────────────────────────────
# SFML 2.x is required only for the `simulador` GUI target. SFML 3 is not API
# compatible, so on Homebrew we hint the keg-only sfml@2 prefix.
option(SIM_BUILD_UI "Build the SFML/ImGui GUI target (simulador)" ON)

if(EXISTS /opt/homebrew/opt/sfml@2)
    list(APPEND CMAKE_PREFIX_PATH /opt/homebrew/opt/sfml@2)
endif()

set(SIM_UI_ENABLED OFF)
if(SIM_BUILD_UI)
    find_package(SFML 2.5 COMPONENTS graphics window system QUIET)
    if(SFML_FOUND)
        set(SIM_UI_ENABLED ON)
    else()
        message(STATUS "SFML 2.5 not found — skipping GUI target `simulador`. "
                       "Install with `brew install sfml@2` or configure with -DSIM_BUILD_UI=OFF.")
    endif()
else()
    message(STATUS "SIM_BUILD_UI=OFF — skipping GUI target `simulador`.")
endif()

if(SIM_UI_ENABLED)
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
endif()  # SIM_UI_ENABLED

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
    src/economics/FundamentalProcessFactory.cpp
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
    # Phase 8
    src/persistence/BinaryLogWriter.cpp
    src/persistence/BinaryLogReader.cpp
    src/persistence/CsvExporter.cpp
    src/persistence/Replay.cpp
    # Phase 9
    src/analysis/AcfComputer.cpp
    src/analysis/HillEstimator.cpp
    src/analysis/FlashCrashDetector.cpp
    src/analysis/Report.cpp
)

add_library(sim_core STATIC ${CORE_SRC})
target_include_directories(sim_core PUBLIC src)
target_compile_options(sim_core PRIVATE
    ${SIM_WARN_FLAGS}
    ${SIM_SANITIZER_FLAGS}
)
target_link_options(sim_core INTERFACE ${SIM_SANITIZER_FLAGS})

# ── simulador executable (GUI, optional) ──────────────────────────────────
if(SIM_UI_ENABLED)
add_executable(simulador
    src/main.cpp
    src/visual/VisualApp.cpp
    src/visual/Controls.cpp
    src/visual/panels/OrderBookHeatmap.cpp
    src/visual/panels/CandleVolumeVwap.cpp
    src/visual/panels/TradeTape.cpp
    src/visual/panels/AgentInspector.cpp
    src/visual/panels/MicrostructureDashboard.cpp
    src/visual/panels/StylizedFactsPanel.cpp
    src/visual/panels/RegimeIndicator.cpp
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
endif()  # SIM_UI_ENABLED

# ── analysis_csv_export: offline CSV export tool (no UI) ──────────────────
add_executable(analysis_csv_export src/tools/analysis_csv_export.cpp)
target_include_directories(analysis_csv_export PRIVATE src)
target_link_libraries(analysis_csv_export PRIVATE sim_core)
target_compile_options(analysis_csv_export PRIVATE ${SIM_WARN_FLAGS})

# ── sim_headless: full simulation without UI (for calibration / CI) ────────
add_executable(sim_headless src/tools/sim_headless.cpp)
target_include_directories(sim_headless PRIVATE src)
target_link_libraries(sim_headless PRIVATE sim_core)
target_compile_options(sim_headless PRIVATE ${SIM_WARN_FLAGS})

# ── sim_calibrate: ensemble harness (N seeds → mean ± std per fact) ─────────
add_executable(sim_calibrate src/tools/sim_calibrate.cpp)
target_include_directories(sim_calibrate PRIVATE src)
target_link_libraries(sim_calibrate PRIVATE sim_core)
target_compile_options(sim_calibrate PRIVATE ${SIM_WARN_FLAGS})

# ── analysis tools (no UI) ─────────────────────────────────────────────────
add_subdirectory(analysis)

# ── Tests ─────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
