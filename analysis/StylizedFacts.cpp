// CLI: analysis_stylized_facts <log.bin> [report.csv]
// Reads a binary simulation log and validates 8 empirical stylized facts.
// Exits 0 if ≥ 6/8 facts pass, exits 1 otherwise.
#include "analysis/Report.h"
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: analysis_stylized_facts <log.bin> [report.csv]\n");
        return 2;
    }

    std::string bin_path = argv[1];
    std::string csv_path = (argc >= 3) ? argv[2] : "";

    AnalysisReport rep = Report::run(bin_path);

    std::fprintf(stdout, "%s\n", rep.to_text().c_str());

    if (!csv_path.empty()) {
        rep.to_csv(csv_path);
        std::fprintf(stdout, "CSV report written to: %s\n", csv_path.c_str());
    }

    // Exit 0 if at least 6/8 pass.
    return (rep.passed_count >= 6) ? 0 : 1;
}
