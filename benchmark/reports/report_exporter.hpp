#ifndef NVXK_REPORT_EXPORTER_HPP
#define NVXK_REPORT_EXPORTER_HPP

#include "../../diagnostics/cpu/cpu_diagnostics.hpp"
#include "../insights/insights_engine.hpp"
#include "../runner/workbench_engine.hpp"
#include <string>
#include <vector>

namespace nvxk_wb {

std::string export_report_markdown(
    const DiagnosticsReport& diag,
    const std::vector<BenchmarkResultRow>& history,
    const std::vector<EngineeringInsight>& insights);

std::string export_report_csv(const std::vector<BenchmarkResultRow>& history);

std::string json_escape(const std::string& s);

}  // namespace nvxk_wb

#endif