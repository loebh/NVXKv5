#include "workbench_app.hpp"
#include "theme.hpp"
#include "../../benchmark/reports/report_exporter.hpp"
#include <imgui.h>
#include <implot.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace nvxk_ui {

namespace {

std::string fmt_bytes(size_t b) {
    char buf[64];
    if (b >= (1024ULL * 1024ULL * 1024ULL)) {
        std::snprintf(buf, sizeof(buf), "%.2f GiB", static_cast<double>(b) / (1024.0 * 1024.0 * 1024.0));
    } else if (b >= (1024ULL * 1024ULL)) {
        std::snprintf(buf, sizeof(buf), "%.2f MiB", static_cast<double>(b) / (1024.0 * 1024.0));
    } else if (b >= 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%.1f KiB", static_cast<double>(b) / 1024.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%zu B", b);
    }
    return std::string(buf);
}

std::string fmt_sci(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2e", v);
    return std::string(buf);
}

bool contains_icase(const std::string& hay, const char* needle) {
    if (!needle || needle[0] == '\0') return true;
    std::string h = hay, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return h.find(n) != std::string::npos;
}

void draw_badge(const char* label, const ImVec4& col) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x, col.y, col.z, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x, col.y, col.z, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(col.x, col.y, col.z, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(col.x, col.y, col.z, 0.45f));
    ImGui::SmallButton(label);
    ImGui::PopStyleColor(5);
}

}  // namespace

WorkbenchApp::WorkbenchApp(nvxk_wb::WorkbenchEngine& engine)
    : engine_(engine), last_poll_(std::chrono::steady_clock::now()) {
    lab_custom_a_.resize(16);
    lab_custom_b_.resize(16);
    for (int i = 0; i < 16; ++i) {
        lab_custom_a_[static_cast<size_t>(i)] = static_cast<double>((i % 7) - 3) * 0.5;
        lab_custom_b_[static_cast<size_t>(i)] = static_cast<double>((i % 5) - 2) * 0.5;
    }
    refresh_state_if_needed(true);
}

void WorkbenchApp::refresh_state_if_needed(bool force) {
    job_status_ = engine_.get_status();
    if (was_running_ && !job_status_.running) {
        force = true; // Benchmark just finished -> refresh immediately
    }
    was_running_ = job_status_.running;

    // Measurement Mode guarantee: skip OS telemetry polling while timed benchmark runs
    if (!force && job_status_.running && job_status_.measurement_mode) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_poll_).count();
    if (!force && elapsed_ms < poll_interval_ms_) {
        return;
    }
    last_poll_ = now;

    diag_ = nvxk_wb::collect_diagnostics_report();
    backends_ = nvxk_wb::list_implemented_backends();
    history_ = engine_.get_history();
    insights_ = nvxk_wb::generate_engineering_insights(diag_, history_);

    if (diag_.telemetry.cpu_util_available) {
        cpu_spark_.push_back(static_cast<float>(diag_.telemetry.cpu_util_percent));
        if (cpu_spark_.size() > 30) cpu_spark_.pop_front();
    }
    if (diag_.telemetry.memory_available) {
        rss_spark_.push_back(static_cast<float>(diag_.telemetry.process_rss_bytes) / (1024.0f * 1024.0f));
        if (rss_spark_.size() > 30) rss_spark_.pop_front();
    }
}

void WorkbenchApp::render_frame() {
    refresh_state_if_needed(false);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags root_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##NVXK_Root", nullptr, root_flags);
    ImGui::PopStyleVar(2);

    render_topbar();

    const float side_w = compact_sidebar_ ? 58.0f : 215.0f;
    ImGui::BeginChild("##SidebarPane", ImVec2(side_w, 0.0f), true);
    render_sidebar();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::Bg);
    ImGui::BeginChild("##MainContentPane", ImVec2(0.0f, 0.0f), false);
    ImGui::Indent(12.0f);
    ImGui::Spacing();

    render_job_banner();
    if (!status_toast_.empty()) {
        ImGui::TextColored(Palette::Amber, "%s", status_toast_.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss")) status_toast_.clear();
    }

    switch (active_page_) {
        case NavPage::Overview:             render_overview(); break;
        case NavPage::MatrixLab:            render_matrix_lab(); break;
        case NavPage::ExecutionIntel:       render_execution_intel(); break;
        case NavPage::CpuArch:              render_cpu_arch(); break;
        case NavPage::Benchmark:            render_benchmark(); break;
        case NavPage::TopFunctions:         render_top_functions(false); break;
        case NavPage::KernelAnalysis:       render_kernel_analysis(); break;
        case NavPage::MemoryAnalysis:       render_memory_analysis(); break;
        case NavPage::NumericalReliability: render_numerical_reliability(); break;
        case NavPage::Diagnostics:          render_diagnostics(); break;
        case NavPage::Reports:              render_reports(); break;
        case NavPage::Settings:             render_settings(); break;
    }

    ImGui::Unindent(12.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();
}

void WorkbenchApp::render_topbar() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::Sidebar);
    ImGui::BeginChild("##TopBar", ImVec2(0.0f, 44.0f), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY(9.0f);
    ImGui::SetCursorPosX(12.0f);

    draw_badge("XK", Palette::Green);
    ImGui::SameLine();
    ImGui::TextUnformatted("NVXK Engineering Workbench");
    ImGui::SameLine(0.0f, 16.0f);

    // Backend selector (only implemented backends)
    ImGui::SetNextItemWidth(175.0f);
    if (!backends_.empty()) {
        if (selected_backend_idx_ >= static_cast<int>(backends_.size())) selected_backend_idx_ = 0;
        if (ImGui::BeginCombo("##TopBackend", backends_[static_cast<size_t>(selected_backend_idx_)].display_name.c_str())) {
            for (int i = 0; i < static_cast<int>(backends_.size()); ++i) {
                const auto& b = backends_[static_cast<size_t>(i)];
                ImGui::BeginDisabled(!b.available);
                if (ImGui::Selectable(b.display_name.c_str(), i == selected_backend_idx_)) {
                    selected_backend_idx_ = i;
                }
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SameLine();
    const char* precs[] = {"FP32 (float)", "FP64 (double)"};
    ImGui::SetNextItemWidth(110.0f);
    ImGui::Combo("##TopPrec", &selected_precision_idx_, precs, 2);

    ImGui::SameLine();
    int mode_i = measurement_mode_ ? 0 : 1;
    const char* modes[] = {"Measurement Mode", "Normal Mode"};
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::Combo("##TopMode", &mode_i, modes, 2)) {
        measurement_mode_ = (mode_i == 0);
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(job_status_.running);
    if (ImGui::Button("Calibrate (~200ms)")) {
        XK_HardwareProfile prof;
        std::string err;
        if (!engine_.run_calibration(200.0, prof, err)) status_toast_ = err;
        else refresh_state_if_needed(true);
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.79f, 0.58f, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_Text, Palette::Green);
    if (ImGui::Button("Run Benchmark")) {
        nvxk_wb::BenchmarkConfig cfg;
        cfg.M = 256; cfg.K = 256; cfg.N = 256;
        cfg.precision = (selected_precision_idx_ == 1) ? "f64" : "f32";
        cfg.backends = {"ikj", "avx2", "avx2_asm", "auto", "auto_asm"};
        cfg.runs = 5;
        cfg.threads = std::max(1, diag_.profile.hw.physical_cores);
        cfg.measurement_mode = measurement_mode_;
        cfg.run_validation = true;
        std::string err;
        if (!engine_.start_benchmark(cfg, err)) status_toast_ = err;
    }
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Refresh")) refresh_state_if_needed(true);
    ImGui::SameLine();
    if (ImGui::Button(compact_sidebar_ ? ">>" : "<<")) compact_sidebar_ = !compact_sidebar_;

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void WorkbenchApp::render_sidebar() {
    auto nav_btn = [&](const char* short_tag, const char* label, NavPage page) {
        const bool active = (active_page_ == page);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.74f, 0.97f, 0.16f));
            ImGui::PushStyleColor(ImGuiCol_Text, Palette::Cyan);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.74f, 0.97f, 0.45f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, Palette::TextDim);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        }
        const char* txt = compact_sidebar_ ? short_tag : label;
        if (ImGui::Button(txt, ImVec2(-1.0f, 28.0f))) active_page_ = page;
        if (compact_sidebar_ && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label);
        ImGui::PopStyleColor(3);
    };

    auto group_hdr = [&](const char* title) {
        if (!compact_sidebar_) {
            ImGui::Spacing();
            ImGui::TextColored(Palette::TextDim, "%s", title);
        }
    };

    group_hdr("INTELLIGENCE");
    nav_btn("OV", "Overview", NavPage::Overview);
    nav_btn("ML", "Vector & Matrix Lab", NavPage::MatrixLab);
    nav_btn("EX", "Execution Intelligence", NavPage::ExecutionIntel);

    group_hdr("PERFORMANCE");
    nav_btn("CP", "CPU Architecture", NavPage::CpuArch);
    nav_btn("BM", "Performance Benchmark", NavPage::Benchmark);
    nav_btn("TF", "Top Functions", NavPage::TopFunctions);
    nav_btn("KA", "Kernel Analysis", NavPage::KernelAnalysis);

    group_hdr("SYSTEM & VERIFICATION");
    nav_btn("MM", "Memory Analysis", NavPage::MemoryAnalysis);
    nav_btn("NR", "Numerical Reliability", NavPage::NumericalReliability);
    nav_btn("DG", "Diagnostics", NavPage::Diagnostics);

    group_hdr("WORKSPACE");
    nav_btn("RP", "Reports", NavPage::Reports);
    nav_btn("ST", "Settings", NavPage::Settings);

    // Bottom status area
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 52.0f);
    ImGui::Separator();
    ImGui::TextColored(job_status_.running ? Palette::Amber : Palette::Green,
                       "%s", job_status_.running ? "BUSY" : "READY");
    if (!compact_sidebar_) {
        ImGui::TextColored(Palette::TextDim, "%dC/%dT • %s",
                           diag_.profile.hw.physical_cores,
                           diag_.profile.hw.logical_threads,
                           diag_.profile.hw.has_avx2 ? "AVX2+FMA" : "Scalar");
    }
}

void WorkbenchApp::render_job_banner() {
    if (!job_status_.running) return;
    ImGui::BeginChild("##JobBanner", ImVec2(-12.0f, 38.0f), true);
    draw_badge(job_status_.measurement_mode ? "MEASUREMENT" : "NORMAL", Palette::Cyan);
    ImGui::SameLine();
    ImGui::TextUnformatted(job_status_.current_step.c_str());
    ImGui::SameLine();
    ImGui::ProgressBar(static_cast<float>(job_status_.progress), ImVec2(220.0f, 18.0f));
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) engine_.cancel_benchmark();
    ImGui::EndChild();
    ImGui::Spacing();
}

void WorkbenchApp::draw_kpi_card(const char* id, const char* title, const std::string& val,
                                 const char* badge, const ImVec4& badge_col,
                                 const std::string& sub_left, const std::string& sub_right,
                                 const std::vector<float>& spark, const char* tooltip) {
    ImGui::BeginChild(id, ImVec2(0.0f, 108.0f), true);
    ImGui::TextColored(Palette::TextDim, "%s", title);
    ImGui::SameLine(ImGui::GetWindowWidth() - 78.0f);
    draw_badge(badge, badge_col);
    ImGui::SetWindowFontScale(1.45f);
    ImGui::TextUnformatted(val.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextColored(Palette::TextDim, "%s  |  %s", sub_left.c_str(), sub_right.c_str());
    if (spark.size() >= 2) {
        ImGui::PlotLines("##sp", spark.data(), static_cast<int>(spark.size()), 0, nullptr,
                         FLT_MAX, FLT_MAX, ImVec2(-1.0f, 22.0f));
    }
    ImGui::EndChild();
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
}

void WorkbenchApp::render_overview() {
    // 5 KPI Cards Row
    if (ImGui::BeginTable("##KpiRow", 5, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();

        // 1. CPU Utilization
        ImGui::TableSetColumnIndex(0);
        std::vector<float> cpu_v(cpu_spark_.begin(), cpu_spark_.end());
        char cpu_buf[32];
        if (diag_.telemetry.cpu_util_available) {
            std::snprintf(cpu_buf, sizeof(cpu_buf), "%.1f%%", diag_.telemetry.cpu_util_percent);
        } else {
            std::snprintf(cpu_buf, sizeof(cpu_buf), "Unavailable");
        }
        draw_kpi_card("##KpiCpu", "CPU UTIL", cpu_buf,
                      diag_.telemetry.cpu_util_available ? "LIVE" : "N/A",
                      diag_.telemetry.cpu_util_available ? Palette::Green : Palette::TextDim,
                      std::to_string(diag_.profile.hw.physical_cores) + "C/" + std::to_string(diag_.profile.hw.logical_threads) + "T",
                      "OS Sample", cpu_v, "Measured OS CPU utilization via /proc/stat or Win32 GetSystemTimes");

        // 2. Throughput
        ImGui::TableSetColumnIndex(1);
        std::vector<float> gf_v, lat_v, err_v;
        const nvxk_wb::BenchmarkResultRow* best = nullptr;
        const nvxk_wb::BenchmarkResultRow* fastest = nullptr;
        double worst_err = 0.0;
        bool any_val = false, any_fail = false;
        for (const auto& r : history_) {
            gf_v.push_back(static_cast<float>(r.gflops));
            lat_v.push_back(static_cast<float>(r.median_ms));
            if (!best || r.gflops > best->gflops) best = &r;
            if (!fastest || r.median_ms < fastest->median_ms) fastest = &r;
            if (r.validation.executed) {
                any_val = true;
                if (!r.validation.passed) any_fail = true;
                if (r.validation.max_rel_error > worst_err) worst_err = r.validation.max_rel_error;
                err_v.push_back(static_cast<float>(r.validation.max_rel_error));
            }
        }
        char gf_buf[32];
        if (best) std::snprintf(gf_buf, sizeof(gf_buf), "%.2f GF", best->gflops);
        else std::snprintf(gf_buf, sizeof(gf_buf), "Awaiting");
        draw_kpi_card("##KpiGf", "THROUGHPUT", gf_buf,
                      best ? best->backend.c_str() : "IDLE",
                      best ? Palette::Green : Palette::TextDim,
                      best ? (std::to_string(best->M) + "x" + std::to_string(best->N)) : "No runs",
                      best && best->has_baseline ? (std::to_string(static_cast<int>(best->speedup_vs_baseline * 10) / 10.0) + "x") : "—",
                      gf_v, "Peak measured GFLOPS (2*M*K*N / median_sec)");

        // 3. Execution Latency
        ImGui::TableSetColumnIndex(2);
        char lat_buf[32];
        if (fastest) std::snprintf(lat_buf, sizeof(lat_buf), "%.3f ms", fastest->median_ms);
        else std::snprintf(lat_buf, sizeof(lat_buf), "Awaiting");
        draw_kpi_card("##KpiLat", "LATENCY", lat_buf,
                      fastest ? "MEDIAN" : "IDLE", Palette::Cyan,
                      fastest ? ("P95: " + fmt_sci(fastest->p95_ms) + "ms") : "No runs",
                      fastest ? ("n=" + std::to_string(fastest->sample_count)) : "—",
                      lat_v, "Median execution latency and P95/P99 percentiles");

        // 4. Memory Performance
        ImGui::TableSetColumnIndex(3);
        std::vector<float> rss_v(rss_spark_.begin(), rss_spark_.end());
        const nvxk_wb::BenchmarkResultRow* last_r = history_.empty() ? nullptr : &history_.back();
        draw_kpi_card("##KpiMem", "MEMORY",
                      diag_.telemetry.memory_available ? fmt_bytes(diag_.telemetry.process_rss_bytes) : "N/A",
                      "RSS", Palette::Cyan,
                      last_r ? ("WS: " + fmt_bytes(last_r->workspace_bytes)) : "WS: 0 B",
                      last_r ? (std::to_string(static_cast<int>(last_r->measured_bandwidth_gbs)) + " GB/s") : "—",
                      rss_v, "Process RSS, NVXK scratchpad workspace, and measured effective bandwidth");

        // 5. Numerical Accuracy
        ImGui::TableSetColumnIndex(4);
        draw_kpi_card("##KpiAcc", "ACCURACY",
                      any_val ? fmt_sci(worst_err) : "Not measured",
                      !any_val ? "UNVERIF" : (any_fail ? "FAIL" : "PASS"),
                      !any_val ? Palette::Amber : (any_fail ? Palette::Red : Palette::Green),
                      "Ref: f64_ikj", any_val ? "Max Rel Err" : "Awaiting",
                      err_v, "Worst max relative error vs independent f64 reference");

        ImGui::EndTable();
    }

    ImGui::Spacing();

    // MIDDLE: Main ImPlot Chart (Left) + Engineering Insights (Right)
    const float avail_w = ImGui::GetContentRegionAvail().x - 12.0f;
    const float chart_w = avail_w * 0.64f;
    const float ins_w = avail_w - chart_w - 10.0f;

    ImGui::BeginChild("##OverviewChartPane", ImVec2(chart_w, 305.0f), true);
    ImGui::TextUnformatted("Performance Telemetry");
    ImGui::SameLine();
    draw_badge("MEASURED", Palette::Green);
    ImGui::SameLine(chart_w - 220.0f);
    const char* metrics[] = {"Throughput (GFLOPS)", "Median Latency (ms)", "Bandwidth (GB/s)",
                             "Workspace (KiB)", "Max Rel Error", "Live CPU (%)"};
    ImGui::SetNextItemWidth(205.0f);
    ImGui::Combo("##ChartMetric", &chart_metric_idx_, metrics, 6);

    if (ImPlot::BeginPlot("##MainPerfPlot", ImVec2(-1.0f, -1.0f))) {
        if (chart_metric_idx_ == 5) {
            ImPlot::SetupAxes("Sample", "CPU %", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            if (!cpu_spark_.empty()) {
                std::vector<float> y(cpu_spark_.begin(), cpu_spark_.end());
                ImPlot::PlotLine("CPU Utilization (%)", y.data(), static_cast<int>(y.size()));
            }
        } else if (!history_.empty()) {
            std::vector<double> vals;
            std::vector<const char*> labels;
            std::vector<std::string> label_storage;
            vals.reserve(history_.size());
            label_storage.reserve(history_.size());
            for (const auto& r : history_) {
                label_storage.push_back(r.backend + " (" + std::to_string(r.M) + ")");
                if (chart_metric_idx_ == 0) vals.push_back(r.gflops);
                else if (chart_metric_idx_ == 1) vals.push_back(r.median_ms);
                else if (chart_metric_idx_ == 2) vals.push_back(r.measured_bandwidth_gbs);
                else if (chart_metric_idx_ == 3) vals.push_back(static_cast<double>(r.workspace_bytes) / 1024.0);
                else vals.push_back(r.validation.max_rel_error);
            }
            for (const auto& s : label_storage) labels.push_back(s.c_str());
            ImPlot::SetupAxes("Backend / Shape", metrics[chart_metric_idx_], ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxisTicks(ImAxis_X1, 0.0, static_cast<double>(vals.size() - 1),
                                   static_cast<int>(vals.size()), labels.data());
            ImPlot::PlotBars(metrics[chart_metric_idx_], vals.data(), static_cast<int>(vals.size()), 0.55);
        } else {
            ImPlot::SetupAxes("Run", metrics[chart_metric_idx_]);
        }
        ImPlot::EndPlot();
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 10.0f);

    // Engineering Insights Panel
    ImGui::BeginChild("##InsightsPane", ImVec2(ins_w, 305.0f), true);
    ImGui::TextUnformatted("Engineering Insights");
    ImGui::SameLine();
    draw_badge(std::to_string(insights_.size()).c_str(), Palette::Cyan);
    ImGui::Separator();
    for (const auto& ins : insights_) {
        ImVec4 col = Palette::Cyan;
        if (ins.severity == "positive") col = Palette::Green;
        else if (ins.severity == "warning") col = Palette::Amber;
        else if (ins.severity == "critical") col = Palette::Red;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::PanelSec);
        ImGui::BeginChild(ins.id.c_str(), ImVec2(-1.0f, 92.0f), true);
        draw_badge(ins.category.c_str(), Palette::TextDim);
        ImGui::SameLine();
        draw_badge(ins.confidence.c_str(), col);
        ImGui::TextWrapped("%s", ins.title.c_str());
        ImGui::TextColored(Palette::Cyan, "%s", ins.supporting_measurement.c_str());
        ImGui::TextColored(Palette::TextDim, "%s", ins.evidence.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    render_top_functions(true);
}

void WorkbenchApp::render_top_functions(bool embedded) {
    ImGui::BeginChild(embedded ? "##TopFnEmb" : "##TopFnFull",
                      ImVec2(-12.0f, embedded ? 260.0f : -12.0f), true);
    ImGui::TextUnformatted("Top Functions & Kernel Performance");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##TblSearch", "Filter function/backend...", table_search_, sizeof(table_search_));

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##TopFnTable", 10, flags, ImVec2(0.0f, embedded ? 150.0f : 380.0f))) {
        ImGui::TableSetupColumn("Function");
        ImGui::TableSetupColumn("Backend");
        ImGui::TableSetupColumn("Shape");
        ImGui::TableSetupColumn("Calls");
        ImGui::TableSetupColumn("Mean (ms)");
        ImGui::TableSetupColumn("Median (ms)");
        ImGui::TableSetupColumn("GFLOPS");
        ImGui::TableSetupColumn("Speedup");
        ImGui::TableSetupColumn("MaxRelErr");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();

        for (const auto& r : history_) {
            if (!contains_icase(r.function_name, table_search_) && !contains_icase(r.backend, table_search_)) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(r.function_name.c_str(), selected_row_id_ == r.id, ImGuiSelectableFlags_SpanAllColumns)) {
                selected_row_id_ = r.id;
            }
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(r.backend.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%zux%zux%zu", r.M, r.K, r.N);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%zu", r.total_calls);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%.3f", r.mean_ms);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%.3f", r.median_ms);
            ImGui::TableSetColumnIndex(6); ImGui::TextColored(Palette::Green, "%.2f", r.gflops);
            ImGui::TableSetColumnIndex(7);
            if (r.has_baseline) ImGui::Text("%.2fx", r.speedup_vs_baseline); else ImGui::TextUnformatted("N/A");
            ImGui::TableSetColumnIndex(8);
            if (r.validation.executed) ImGui::Text("%.2e", r.validation.max_rel_error); else ImGui::TextUnformatted("Not measured");
            ImGui::TableSetColumnIndex(9);
            if (!r.validation.executed) ImGui::TextColored(Palette::Amber, "UNVERIFIED");
            else if (r.validation.passed) ImGui::TextColored(Palette::Green, "PASS");
            else ImGui::TextColored(Palette::Red, "FAIL");
        }
        ImGui::EndTable();
    }

    // Selected row detailed inspector
    for (const auto& r : history_) {
        if (r.id == selected_row_id_) {
            ImGui::Separator();
            ImGui::TextColored(Palette::Cyan, "Inspector: %s | Kernel=%s | Threads=%d | Blocks=(%zu,%zu,%zu) | WS=%s",
                               r.function_name.c_str(), r.kernel_selected.c_str(), r.threads_used,
                               r.mc, r.kc, r.nc, fmt_bytes(r.workspace_bytes).c_str());
            if (!r.plan_reason.empty()) {
                ImGui::TextColored(Palette::TextDim, "Planner: %s", r.plan_reason.c_str());
            }
            break;
        }
    }
    ImGui::EndChild();
}

void WorkbenchApp::render_matrix_lab() {
    ImGui::BeginChild("##LabCfg", ImVec2(-12.0f, 120.0f), true);
    ImGui::TextUnformatted("Vector & Matrix Lab (Real NVXK Execution)");
    const char* ops[] = {"Matrix Multiply (C = A x B)", "Vector Add (c = a + b)"};
    const char* precs[] = {"FP32 (float)", "FP64 (double)", "INT32 -> INT64"};
    ImGui::SetNextItemWidth(200.0f); ImGui::Combo("Operation", &lab_op_idx_, ops, 2);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f); ImGui::Combo("Precision", &lab_prec_idx_, precs, 3);
    ImGui::SetNextItemWidth(80.0f); ImGui::InputInt("M", &lab_m_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(80.0f); ImGui::InputInt("K", &lab_k_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(80.0f); ImGui::InputInt("N", &lab_n_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(70.0f); ImGui::InputInt("Threads", &lab_threads_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(70.0f); ImGui::InputInt("Seed", &lab_seed_);
    lab_m_ = std::max(1, std::min(2048, lab_m_));
    lab_k_ = std::max(1, std::min(2048, lab_k_));
    lab_n_ = std::max(1, std::min(2048, lab_n_));

    ImGui::SameLine();
    if (ImGui::Button("Execute Operation")) {
        const std::string op = (lab_op_idx_ == 1) ? "vector_add" : "gemm";
        const std::string pr = (lab_prec_idx_ == 0) ? "f32" : (lab_prec_idx_ == 1 ? "f64" : "i32_i64");
        const std::string bk = backends_.empty() ? "auto_asm" : backends_[static_cast<size_t>(selected_backend_idx_)].id;
        lab_result_ = engine_.run_matrix_lab(op, pr, bk,
                                             static_cast<size_t>(lab_m_), static_cast<size_t>(lab_k_), static_cast<size_t>(lab_n_),
                                             lab_threads_, static_cast<size_t>(bench_budget_kb_) * 1024,
                                             static_cast<uint32_t>(lab_seed_), true);
        lab_has_result_ = true;
        refresh_state_if_needed(true);
    }
    ImGui::EndChild();

    if (lab_has_result_) {
        ImGui::BeginChild("##LabRes", ImVec2(-12.0f, -12.0f), true);
        if (!lab_result_.success) {
            ImGui::TextColored(Palette::Red, "Error: %s", lab_result_.error_message.c_str());
        } else {
            ImGui::Text("Elapsed: %.4f ms | Throughput: %.2f GFLOPS | Threads: %d | 64B Aligned: %s",
                        lab_result_.elapsed_ms, lab_result_.gflops, lab_result_.threads_used,
                        lab_result_.aligned_64b ? "Yes" : "No");
            ImGui::TextColored(lab_result_.validation.passed ? Palette::Green : Palette::Red,
                               "Validation (%s): %s | MaxAbs=%.2e | MaxRel=%.2e | RMSE=%.2e",
                               lab_result_.validation.reference_backend.c_str(),
                               lab_result_.validation.passed ? "PASS" : "FAIL",
                               lab_result_.validation.max_abs_error,
                               lab_result_.validation.max_rel_error,
                               lab_result_.validation.rmse);
            if (!lab_result_.c_preview.empty() && ImPlot::BeginPlot("Output Matrix C Heatmap (<=16x16)", ImVec2(-1.0f, 340.0f))) {
                ImPlot::PlotHeatmap("C", lab_result_.c_preview.data(),
                                    static_cast<int>(lab_result_.c_preview_rows),
                                    static_cast<int>(lab_result_.c_preview_cols));
                ImPlot::EndPlot();
            }
        }
        ImGui::EndChild();
    }
}

void WorkbenchApp::render_execution_intel() {
    ImGui::BeginChild("##ConceptWarn", ImVec2(-12.0f, 64.0f), true);
    ImGui::TextColored(Palette::Amber,
                       "[CONCEPTUAL VISUALIZATION NOTICE] AVX2 lane diagrams illustrate NVXK's micro_6x16_full_asm_f32 algorithm geometry; "
                       "they do NOT read live CPU hardware registers. Planner output below is [INSTRUMENTED RUNTIME EVIDENCE].");
    ImGui::EndChild();

    ImGui::BeginChild("##PlanInspector", ImVec2(-12.0f, -12.0f), true);
    ImGui::TextUnformatted("Live Instrumented Planner Query (XK_plan_gemm)");
    ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("M##p", &plan_m_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("K##p", &plan_k_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("N##p", &plan_n_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(100.0f); ImGui::InputInt("Budget KiB##p", &plan_budget_kb_);
    ImGui::SameLine();
    if (ImGui::Button("Inspect Plan")) {
        std::string err;
        const std::string bk = backends_.empty() ? "auto_asm" : backends_[static_cast<size_t>(selected_backend_idx_)].id;
        if (nvxk_wb::inspect_gemm_plan(static_cast<size_t>(std::max(1, plan_m_)),
                                       static_cast<size_t>(std::max(1, plan_k_)),
                                       static_cast<size_t>(std::max(1, plan_n_)),
                                       selected_precision_idx_ == 1 ? "f64" : "f32",
                                       bk, std::max(1, diag_.profile.hw.physical_cores),
                                       static_cast<size_t>(std::max(0, plan_budget_kb_)) * 1024,
                                       inspected_plan_, err)) {
            has_inspected_plan_ = true;
        } else {
            status_toast_ = err;
        }
    }
    if (has_inspected_plan_) {
        ImGui::Separator();
        ImGui::Text("Kernel: %s | Axis: %s | Threads: %d",
                    nvxk_wb::kernel_kind_to_string(inspected_plan_.kernel).c_str(),
                    nvxk_wb::parallel_axis_to_string(inspected_plan_.axis).c_str(),
                    inspected_plan_.threads);
        ImGui::Text("Blocking: MC=%zu, KC=%zu, NC=%zu | Workspace: %s | Clamped: %s | SMT L2-Halved: %s",
                    inspected_plan_.mc, inspected_plan_.kc, inspected_plan_.nc,
                    fmt_bytes(inspected_plan_.required_workspace_bytes).c_str(),
                    inspected_plan_.explanation.budget_clamped ? "Yes" : "No",
                    inspected_plan_.explanation.smt_l2_shared ? "Yes" : "No");
        ImGui::TextWrapped("Rationale: %s", inspected_plan_.explanation.reason.c_str());
    }
    ImGui::EndChild();
}

void WorkbenchApp::render_cpu_arch() {
    ImGui::BeginChild("##CpuSummary", ImVec2(-12.0f, 92.0f), true);
    ImGui::Text("CPU: %s (%s)  |  Topology: %d Physical Cores / %d Logical Threads",
                diag_.profile.hw.brand.c_str(), diag_.profile.hw.vendor.c_str(),
                diag_.profile.hw.physical_cores, diag_.profile.hw.logical_threads);
    ImGui::Text("Caches: L1d=%s  |  L2=%s  |  L3=%s",
                fmt_bytes(diag_.profile.hw.l1d_bytes).c_str(),
                fmt_bytes(diag_.profile.hw.l2_bytes).c_str(),
                fmt_bytes(diag_.profile.hw.l3_bytes).c_str());
    ImGui::Text("Profile (%s): 1T=%.1f GF  |  Mem BW=%.1f GB/s  |  OMP Fork=%.1f us  |  SMT Eff=%.2f",
                diag_.profile.calibrated ? "Calibrated" : "Default",
                diag_.profile.peak_gflops_1t, diag_.profile.mem_bandwidth_gbs,
                diag_.profile.omp_fork_overhead_us, diag_.profile.smt_efficiency);
    ImGui::EndChild();

    ImGui::BeginChild("##IsaTablePane", ImVec2(-12.0f, -12.0f), true);
    ImGui::TextUnformatted("5-Stage ISA Capability Verification Matrix (Never Collapsed)");
    if (ImGui::BeginTable("##IsaMatrix", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Feature");
        ImGui::TableSetupColumn("1. CPU");
        ImGui::TableSetupColumn("2. Compiler");
        ImGui::TableSetupColumn("3. Binary");
        ImGui::TableSetupColumn("4. Dispatch");
        ImGui::TableSetupColumn("5. Executed");
        ImGui::TableSetupColumn("Notes");
        ImGui::TableHeadersRow();
        for (const auto& f : diag_.isa_matrix) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(f.feature_name.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextColored(f.cpu_supports ? Palette::Green : Palette::TextDim, "%s", f.cpu_supports ? "YES" : "NO");
            ImGui::TableSetColumnIndex(2); ImGui::TextColored(f.compiler_supports ? Palette::Green : Palette::TextDim, "%s", f.compiler_supports ? "YES" : "NO");
            ImGui::TableSetColumnIndex(3); ImGui::TextColored(f.binary_contains ? Palette::Green : Palette::TextDim, "%s", f.binary_contains ? "YES" : "NO");
            ImGui::TableSetColumnIndex(4); ImGui::TextColored(f.runtime_dispatch_selected ? Palette::Cyan : Palette::TextDim, "%s", f.runtime_dispatch_selected ? "SELECTED" : "NO");
            ImGui::TableSetColumnIndex(5); ImGui::TextColored(f.execution_verified ? Palette::Green : Palette::Amber, "%s", f.execution_verified ? "VERIFIED" : "NOT YET");
            ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(f.notes.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void WorkbenchApp::render_benchmark() {
    ImGui::BeginChild("##BenchCfg", ImVec2(-12.0f, -12.0f), true);
    ImGui::TextUnformatted("Configure & Launch Asynchronous NVXK Benchmark");
    ImGui::Separator();

    ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("M##b", &bench_m_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("K##b", &bench_k_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("N##b", &bench_n_);
    ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("Runs", &bench_runs_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f); ImGui::InputInt("Manual OMP Threads", &bench_threads_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(110.0f); ImGui::InputInt("Workspace Budget (KiB)", &bench_budget_kb_);

    ImGui::Checkbox("Measurement Mode (pause telemetry during timed loops)", &measurement_mode_);
    ImGui::Checkbox("Cold-Cache Mode (flush L3 strictly outside timer)", &bench_cold_cache_);
    ImGui::Checkbox("Validate every backend vs f64 reference", &bench_validate_);

    ImGui::Spacing();
    ImGui::TextColored(Palette::TextDim, "Select Backends:");
    for (size_t i = 0; i < backends_.size() && i < 8; ++i) {
        ImGui::BeginDisabled(!backends_[i].available);
        bool flg = (bench_bk_flags_[i] != 0); if (ImGui::Checkbox(backends_[i].id.c_str(), &flg)) bench_bk_flags_[i] = flg ? 1 : 0;
        if (i + 1 < backends_.size()) ImGui::SameLine();
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(job_status_.running);
    if (ImGui::Button("Launch Benchmark Session")) {
        nvxk_wb::BenchmarkConfig cfg;
        cfg.M = static_cast<size_t>(std::max(1, bench_m_));
        cfg.K = static_cast<size_t>(std::max(1, bench_k_));
        cfg.N = static_cast<size_t>(std::max(1, bench_n_));
        cfg.precision = (selected_precision_idx_ == 1) ? "f64" : "f32";
        cfg.backends.clear();
        for (size_t i = 0; i < backends_.size() && i < 8; ++i) {
            if (backends_[i].available && bench_bk_flags_[i]) cfg.backends.push_back(backends_[i].id);
        }
        cfg.runs = std::max(1, bench_runs_);
        cfg.threads = std::max(1, bench_threads_);
        cfg.memory_budget_bytes = static_cast<size_t>(std::max(64, bench_budget_kb_)) * 1024;
        cfg.cold_cache = bench_cold_cache_;
        cfg.measurement_mode = measurement_mode_;
        cfg.run_validation = bench_validate_;
        std::string err;
        if (!engine_.start_benchmark(cfg, err)) status_toast_ = err;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear History")) { engine_.clear_history(); refresh_state_if_needed(true); }
    ImGui::EndChild();
}

void WorkbenchApp::render_kernel_analysis() {
    ImGui::BeginChild("##KernelReg", ImVec2(-12.0f, -12.0f), true);
    ImGui::TextUnformatted("Implemented NVXK Backends Registry");
    if (ImGui::BeginTable("##KerRegTbl", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Category");
        ImGui::TableSetupColumn("Available");
        ImGui::TableSetupColumn("Notes");
        ImGui::TableHeadersRow();
        for (const auto& b : backends_) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(b.id.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(b.display_name.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(b.category.c_str());
            ImGui::TableSetColumnIndex(3); ImGui::TextColored(b.available ? Palette::Green : Palette::Red, "%s", b.available ? "YES" : "NO");
            ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(b.availability_note.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void WorkbenchApp::render_memory_analysis() {
    ImGui::BeginChild("##MemPane", ImVec2(-12.0f, -12.0f), true);
    ImGui::Text("Process RSS: %s  |  Peak RSS: %s  |  System Avail: %s",
                fmt_bytes(diag_.telemetry.process_rss_bytes).c_str(),
                fmt_bytes(diag_.telemetry.process_peak_rss_bytes).c_str(),
                fmt_bytes(diag_.telemetry.system_avail_ram_bytes).c_str());
    ImGui::Text("Calibrated Streaming BW: %.2f GB/s", diag_.profile.mem_bandwidth_gbs);
    if (!history_.empty()) {
        const auto& r = history_.back();
        ImGui::Text("Latest Run Working Set: %s  |  NVXK Workspace: %s  |  Measured Effective BW: %.2f GB/s",
                    fmt_bytes(r.working_set_bytes).c_str(), fmt_bytes(r.workspace_bytes).c_str(),
                    r.measured_bandwidth_gbs);
        auto fit_bar = [&](const char* name, size_t cap) {
            float ratio = std::min(1.0f, static_cast<float>(r.working_set_bytes) / static_cast<float>(std::max<size_t>(1, cap)));
            ImGui::Text("%s (%s):", name, fmt_bytes(cap).c_str());
            ImGui::ProgressBar(ratio, ImVec2(320.0f, 16.0f));
        };
        fit_bar("L1d", diag_.profile.hw.l1d_bytes);
        fit_bar("L2", diag_.profile.hw.l2_bytes);
        fit_bar("L3", diag_.profile.hw.l3_bytes);
    }
    ImGui::Spacing();
    ImGui::TextColored(Palette::Amber, "Hardware Cache Hit-Rate Counters: %s", diag_.telemetry.hw_counters_note.c_str());
    ImGui::EndChild();
}

void WorkbenchApp::render_numerical_reliability() {
    ImGui::BeginChild("##RelPane", ImVec2(-12.0f, -12.0f), true);
    if (ImGui::Button("Run 7-Shape Edge-Case Verification Suite")) {
        std::string err;
        const std::string bk = backends_.empty() ? "auto_asm" : backends_[static_cast<size_t>(selected_backend_idx_)].id;
        rel_suite_ = engine_.run_reliability_suite(selected_precision_idx_ == 1 ? "f64" : "f32", bk, 42, err);
        if (!err.empty()) status_toast_ = err;
        refresh_state_if_needed(true);
    }
    if (!rel_suite_.empty()) {
        if (ImGui::BeginTable("##RelTbl", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Shape"); ImGui::TableSetupColumn("Precision");
            ImGui::TableSetupColumn("MaxAbsErr"); ImGui::TableSetupColumn("MaxRelErr");
            ImGui::TableSetupColumn("RMSE"); ImGui::TableSetupColumn("NaN/Inf");
            ImGui::TableSetupColumn("Status"); ImGui::TableHeadersRow();
            for (const auto& v : rel_suite_) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%zux%zux%zu", v.M, v.K, v.N);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(v.precision.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.2e", v.max_abs_error);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.2e", v.max_rel_error);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%.2e", v.rmse);
                ImGui::TableSetColumnIndex(5); ImGui::Text("%zu", v.nan_inf_count);
                ImGui::TableSetColumnIndex(6); ImGui::TextColored(v.passed ? Palette::Green : Palette::Red, "%s", v.passed ? "PASS" : "FAIL");
            }
            ImGui::EndTable();
        }
        const auto& last = rel_suite_.back();
        if (!last.error_heatmap.empty() && ImPlot::BeginPlot("Error Heatmap (128x128x128)", ImVec2(-1.0f, 280.0f))) {
            ImPlot::PlotHeatmap("AbsErr", last.error_heatmap.data(),
                                static_cast<int>(last.heatmap_rows),
                                static_cast<int>(last.heatmap_cols));
            ImPlot::EndPlot();
        }
    }
    ImGui::EndChild();
}

void WorkbenchApp::render_diagnostics() {
    ImGui::BeginChild("##DiagPane", ImVec2(-12.0f, -12.0f), true);
    ImGui::Text("OS: %s | Compiler: %s | Standard: %s", diag_.os_name.c_str(),
                diag_.compiler_name.c_str(), diag_.cpp_standard.c_str());
    ImGui::Text("OpenMP: %s (max_threads=%d)", diag_.openmp_enabled ? "Enabled" : "Disabled", diag_.openmp_max_threads);
    ImGui::Text("Hardware Counters: %s", diag_.telemetry.hw_counters_note.c_str());
    ImGui::EndChild();
}

void WorkbenchApp::render_reports() {
    ImGui::BeginChild("##RepPane", ImVec2(-12.0f, -12.0f), true);
    if (ImGui::Button("Generate Markdown Preview")) {
        report_preview_ = nvxk_wb::export_report_markdown(diag_, history_, insights_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save nvxk_report.md")) {
        std::ofstream out("nvxk_report.md");
        out << nvxk_wb::export_report_markdown(diag_, history_, insights_);
        status_toast_ = "Saved nvxk_report.md to working directory.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Save nvxk_results.csv")) {
        std::ofstream out("nvxk_results.csv");
        out << nvxk_wb::export_report_csv(history_);
        status_toast_ = "Saved nvxk_results.csv to working directory.";
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", report_preview_.empty() ? "Click 'Generate Markdown Preview' or save directly to disk." : report_preview_.c_str());
    ImGui::EndChild();
}

void WorkbenchApp::render_settings() {
    ImGui::BeginChild("##SetPane", ImVec2(-12.0f, -12.0f), true);
    ImGui::SliderInt("Telemetry Poll Interval (ms)", &poll_interval_ms_, 500, 10000);
    ImGui::Checkbox("Default Measurement Mode", &measurement_mode_);
    ImGui::Checkbox("Compact Sidebar", &compact_sidebar_);
    ImGui::EndChild();
}


void WorkbenchDesktopApp::save_ui_config() const {
    std::ofstream out("nvxk_workbench.ini");
    if (!out) return;
    out << "light_theme=" << (light_theme_ ? 1 : 0) << "\n";
    out << "bench_threads=" << bench_threads_ << "\n";
    out << "bench_budget_mb=" << bench_budget_mb_ << "\n";
    out << "bench_prec_idx=" << bench_prec_idx_ << "\n";
}

void WorkbenchDesktopApp::load_ui_config() {
    std::ifstream in("nvxk_workbench.ini");
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "light_theme") light_theme_ = (v == "1");
        else if (k == "bench_threads") bench_threads_ = std::max(1, std::atoi(v.c_str()));
        else if (k == "bench_budget_mb") bench_budget_mb_ = std::max(1, std::atoi(v.c_str()));
        else if (k == "bench_prec_idx") bench_prec_idx_ = std::max(0, std::min(1, std::atoi(v.c_str())));
    }
}

}  // namespace nvxk_ui