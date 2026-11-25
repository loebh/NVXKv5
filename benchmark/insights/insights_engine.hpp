#ifndef NVXK_INSIGHTS_ENGINE_HPP
#define NVXK_INSIGHTS_ENGINE_HPP

#include "../../diagnostics/cpu/cpu_diagnostics.hpp"
#include "../runner/workbench_engine.hpp"
#include <string>
#include <vector>

namespace nvxk_wb {

struct EngineeringInsight {
    std::string id;
    std::string category;    // "Performance", "CPU", "Memory", "Numerical Reliability"
    std::string title;
    std::string severity;    // "positive", "info", "warning", "critical"
    std::string confidence;  // "Confirmed", "Suspected", "Informational", "Unverified"
    std::string supporting_measurement;
    std::string evidence;
    std::string suggested_investigation;
};

// Generates truthful, evidence-backed engineering insights strictly from real diagnostics & history.
std::vector<EngineeringInsight> generate_engineering_insights(
    const DiagnosticsReport& diag,
    const std::vector<BenchmarkResultRow>& history);

}  // namespace nvxk_wb

#endif