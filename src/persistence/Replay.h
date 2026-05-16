#pragma once
// Deterministic replay support.
// Since all randomness derives from RngService (splitmix64, fixed seed),
// replaying with the same seed + config produces a bit-identical run on
// the same architecture.
//
// Replay::load_config()  — reads seed/params from an existing .bin log
//                          and returns a SimulationConfig ready to re-run.
// Replay::configs_match() — compares two logs record-by-record for equality.
//
// Note: cross-architecture determinism is NOT guaranteed (libm may differ
// between x86 and ARM for transcendental functions used by GBM / news process).
#include "BinaryLogReader.h"
#include "core/Config.h"
#include <string>

class Replay {
public:
    // Populate out_cfg from the FileHeaderRecord of bin_path.
    // Returns false if the file cannot be opened or has an invalid header.
    static bool load_config(const std::string& bin_path, SimulationConfig& out_cfg);

    // Compare two binary logs record-by-record.
    // Returns true iff both files have identical byte sequences.
    static bool logs_identical(const std::string& path_a,
                               const std::string& path_b);

    // Count specific record types in a log (for verification/reporting).
    struct LogStats {
        uint64_t trades;
        uint64_t snapshots;
        uint64_t news_events;
        uint64_t regime_changes;
        uint64_t total_records;
    };
    static LogStats count_records(const std::string& path);
};
