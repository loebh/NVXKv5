#ifndef NVXK_WORKBENCH_APP_HPP
#define NVXK_WORKBENCH_APP_HPP

#include "../../adapters/core_adapter/nvxk_adapter.hpp"
#include "../../benchmark/insights/insights_engine.hpp"
#include "../../benchmark/runner/workbench_engine.hpp"
#include "../../diagnostics/cpu/cpu_diagnostics.hpp"
#include <imgui.h>
#include <chrono>
#include <deque>
#include <string>
#include <vector>

namespace nvxk_ui {

enum class NavPage {
    Overview = 0,
    MatrixLab,
    ExecutionIntel,
    CpuArch,
    Benchmark,
    TopFunctions,
    KernelAnalysis,
    MemoryAnalysis,
    NumericalReliability,
    Diagnostics,
    Reports,
    Settings
};

class WorkbenchApp {
public:
    explicit WorkbenchApp(nvxk_wb::WorkbenchEngine& engine);
    void render_frame();

private:
    void refresh_state_if_needed(bool force = false);
    void render_topbar();
    void render_sidebar();
    void render_job_banner();

    // 12 Specialized Workspaces
    void render_overview();
    void render_matrix_lab();
    void render_execution_intel();
    void render_cpu_arch();
    void render_benchmark();
    void render_top_functions(bool embedded);
    void render_kernel_analysis();
    void render_memory_analysis();
    void render_numerical_reliability();
    void render_diagnostics();
    void render_reports();
    void render_settings();

    void draw_kpi_card(const char* id, const char* title, const std::string& val,
                       const char* badge, const ImVec4& badge_col,
                       const std::string& sub_left, const std::string& sub_right,
                       const std::vector<float>& spark, const char* tooltip);

    nvxk_wb::WorkbenchEngine& engine_;
    NavPage active_page_ = NavPage::Overview;
    bool compact_sidebar_ = false;

    // Cached snapshot refreshed outside Measurement Mode timed loops
    nvxk_wb::DiagnosticsReport diag_;
    std::vector<nvxk_wb::BackendInfo> backends_;
    std::vector<nvxk_wb::BenchmarkResultRow> history_;
    std::vector<nvxk_wb::EngineeringInsight> insights_;
    nvxk_wb::JobStatus job_status_;
    bool was_running_ = false;

    std::chrono::steady_clock::time_point last_poll_;
    int poll_interval_ms_ = 2000;
    std::deque<float> cpu_spark_;
    std::deque<float> rss_spark_;

    // Top bar & filter controls
    int selected_session_idx_ = 0;
    int selected_backend_idx_ = 0;
    int selected_precision_idx_ = 0; // 0 = f32, 1 = f64
    bool measurement_mode_ = true;
    int chart_metric_idx_ = 0;       // 0=GFLOPS, 1=Latency, 2=BW, 3=WS, 4=Error, 5=CPU%
    char table_search_[128] = "";
    uint64_t selected_row_id_ = 0;

    // Benchmark Config UI state
    int bench_m_ = 256, bench_k_ = 256, bench_n_ = 256;
    int bench_runs_ = 5;
    int bench_threads_ = 2;
    int bench_budget_kb_ = 65536;
    bool bench_cold_cache_ = false;
    bool bench_validate_ = true;
    std::vector<uint8_t> bench_bk_flags_;
    std::string status_toast_;

    // Matrix Lab state
    int lab_op_idx_ = 0;   // 0=GEMM, 1=Vector Add
    int lab_prec_idx_ = 0; // 0=f32, 1=f64, 2=i32->i64
    int lab_m_ = 4, lab_k_ = 4, lab_n_ = 4;
    int lab_threads_ = 2;
    int lab_seed_ = 42;
    std::vector<double> lab_custom_a_;
    std::vector<double> lab_custom_b_;
    nvxk_wb::MatrixRunSummary lab_result_;
    bool lab_has_result_ = false;

    // Execution Planner Inspector state
    int plan_m_ = 512, plan_k_ = 512, plan_n_ = 512, plan_budget_kb_ = 65536;
    XK_ExecutionPlan inspected_plan_;
    bool has_inspected_plan_ = false;

    // Numerical Reliability Suite state
    std::vector<nvxk_wb::ValidationMetrics> rel_suite_;

    // Report preview buffer
    std::string report_preview_;
};

}  // namespace nvxk_ui

#endif