#!/usr/bin/env python3
"""
NVXK Complete Documentation System — Reusable HTML-to-PDF Build Pipeline.
Converts structured Vietnamese Markdown sources + inline SVG diagrams + Matplotlib charts
into 8 publication-grade PDF deliverables using WeasyPrint (2-pass exact TOC page numbering)
and renders preview PNGs via pypdfium2 for Visual QA.
"""
import html
import os
import re
import shutil
import zipfile
import markdown
import pypdfium2 as pdfium
import weasyprint

BASE_DIR = "/working_dir/c_03e52d0f508725d4"
INTERMEDIATE_DIR = os.path.join(BASE_DIR, "artifacts/file_generation/ttl=63d/intermediate")
DOCS_DIR = os.path.join(INTERMEDIATE_DIR, "docs")
ASSETS_DIR = os.path.join(DOCS_DIR, "assets")
OUT_DIR = os.path.join(BASE_DIR, "artifacts/file_generation/ttl=63d/output")

os.makedirs(OUT_DIR, exist_ok=True)
os.makedirs(os.path.join(DOCS_DIR, "scripts"), exist_ok=True)

# ---------------------------------------------------------------------------
# Crisp Inline SVG Technical Diagrams (Vector-Sharp, Vietnamese Unicode)
# ---------------------------------------------------------------------------
SVG_SYSTEM_ARCH = r"""
<figure>
<svg viewBox="0 0 740 285" xmlns="http://www.w3.org/2000/svg" style="width:100%;height:auto;font-family:'Inter','Noto Sans','DejaVu Sans',sans-serif;">
  <rect x="0" y="0" width="740" height="285" rx="8" fill="#F8FAFC" stroke="#CBD5E1" stroke-width="1"/>
  <rect x="20" y="18" width="210" height="54" rx="6" fill="#0F2942"/>
  <text x="125" y="40" text-anchor="middle" fill="#FFFFFF" font-size="11.5" font-weight="700">Desktop App (ImGui)</text>
  <text x="125" y="57" text-anchor="middle" fill="#CBD5E1" font-size="9.5">nvxk_desktop.exe (OpenGL 3.3)</text>

  <rect x="265" y="18" width="210" height="54" rx="6" fill="#0F2942"/>
  <text x="370" y="40" text-anchor="middle" fill="#FFFFFF" font-size="11.5" font-weight="700">Web UI (100% Offline)</text>
  <text x="370" y="57" text-anchor="middle" fill="#CBD5E1" font-size="9.5">ui/web/index.html (HTML5/JS)</text>

  <rect x="510" y="18" width="210" height="54" rx="6" fill="#1E3A5F"/>
  <text x="615" y="40" text-anchor="middle" fill="#FFFFFF" font-size="11.5" font-weight="700">CLI &amp; External FFI</text>
  <text x="615" y="57" text-anchor="middle" fill="#CBD5E1" font-size="9.5">nvxk.exe / my_app / C99 / Python</text>

  <line x1="125" y1="72" x2="125" y2="104" stroke="#0D9488" stroke-width="2"/>
  <polygon points="120,98 130,98 125,106" fill="#0D9488"/>
  <line x1="370" y1="72" x2="370" y2="104" stroke="#0D9488" stroke-width="2"/>
  <polygon points="365,98 375,98 370,106" fill="#0D9488"/>
  <text x="435" y="92" fill="#0F766E" font-size="9" font-weight="600">HTTP/REST 127.0.0.1:8765</text>

  <rect x="20" y="108" width="455" height="56" rx="6" fill="#E6F4F1" stroke="#0D9488" stroke-width="1.5"/>
  <text x="247" y="131" text-anchor="middle" fill="#0F2942" font-size="11.5" font-weight="700">NVXK Workbench Engine (C++17 — nvxk_workbench_engine)</text>
  <text x="247" y="149" text-anchor="middle" fill="#334155" font-size="9.5">Async Runner (std::thread) • Core Adapter • 5-Stage ISA Diagnostics • Insights • Report Exporter</text>

  <line x1="615" y1="72" x2="615" y2="188" stroke="#475569" stroke-width="2"/>
  <polygon points="610,182 620,182 615,190" fill="#475569"/>
  <text x="623" y="138" fill="#475569" font-size="9">include/nvxk.h</text>
  <text x="623" y="151" fill="#475569" font-size="9">include/nvxk_c.h</text>

  <line x1="247" y1="164" x2="247" y2="188" stroke="#0D9488" stroke-width="2"/>
  <polygon points="242,182 252,182 247,190" fill="#0D9488"/>

  <rect x="20" y="192" width="700" height="44" rx="6" fill="#0D9488"/>
  <text x="370" y="211" text-anchor="middle" fill="#FFFFFF" font-size="11.5" font-weight="700">NVXK v2 Core (C++11): API • HW Discovery • Roofline Planner • 64B Workspace • AVX2/ASM/ThinM/Scalar</text>
  <text x="370" y="226" text-anchor="middle" fill="#CCFBF1" font-size="9.5">Runtime Dispatch không gắn -mavx2 toàn cục • Suy giảm mềm theo ngân sách RAM</text>

  <line x1="370" y1="236" x2="370" y2="250" stroke="#0F2942" stroke-width="2"/>
  <polygon points="365,246 375,246 370,253" fill="#0F2942"/>

  <rect x="140" y="253" width="460" height="24" rx="4" fill="#1E293B"/>
  <text x="370" y="269" text-anchor="middle" fill="#F8FAFC" font-size="10" font-weight="600">CPU Phổ thông (Tham chiếu: Intel Core i7-7700 4C/8T AVX2+FMA | Fallback Scalar trên mọi CPU)</text>
</svg>
<figcaption>Hình 1: Sơ đồ kiến trúc phân lớp thực tế của hệ thống NVXK v2 và Engineering Workbench (đối chiếu 1:1 từ mã nguồn).</figcaption>
</figure>
"""

SVG_DESKTOP_WIREFRAME = r"""
<figure>
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" style="width:100%;height:auto;font-family:'Inter','Noto Sans','DejaVu Sans',sans-serif;">
  <rect x="0" y="0" width="740" height="310" rx="8" fill="#111315" stroke="#30353B" stroke-width="1.5"/>
  <rect x="0" y="0" width="740" height="38" rx="6" fill="#17191C" stroke="#30353B" stroke-width="1"/>
  <rect x="12" y="9" width="24" height="20" rx="3" fill="#26C995"/>
  <text x="24" y="23" text-anchor="middle" fill="#111315" font-size="10" font-weight="800">XK</text>
  <text x="44" y="23" fill="#E6E9ED" font-size="11" font-weight="700">NVXK Engineering Workbench</text>
  <rect x="225" y="9" width="95" height="20" rx="3" fill="#22262B" stroke="#30353B"/>
  <text x="272" y="22" text-anchor="middle" fill="#929AA4" font-size="9">Backend ▼</text>
  <rect x="328" y="9" width="72" height="20" rx="3" fill="#22262B" stroke="#30353B"/>
  <text x="364" y="22" text-anchor="middle" fill="#929AA4" font-size="9">FP32 ▼</text>
  <rect x="408" y="9" width="95" height="20" rx="3" fill="#22262B" stroke="#30353B"/>
  <text x="455" y="22" text-anchor="middle" fill="#929AA4" font-size="9">Measurement ▼</text>
  <rect x="511" y="9" width="74" height="20" rx="3" fill="#22262B" stroke="#E9B45A"/>
  <text x="548" y="22" text-anchor="middle" fill="#E9B45A" font-size="9" font-weight="600">Calibrate</text>
  <rect x="592" y="9" width="88" height="20" rx="3" fill="#16362E" stroke="#26C995"/>
  <text x="636" y="22" text-anchor="middle" fill="#26C995" font-size="9" font-weight="600">Run Benchmark</text>
  <rect x="688" y="9" width="40" height="20" rx="3" fill="#22262B" stroke="#30353B"/>
  <text x="708" y="22" text-anchor="middle" fill="#E6E9ED" font-size="9">&lt;&lt;</text>

  <rect x="0" y="38" width="150" height="272" fill="#17191C" stroke="#30353B" stroke-width="1"/>
  <text x="12" y="58" fill="#929AA4" font-size="8.5" font-weight="700">INTELLIGENCE</text>
  <rect x="8" y="64" width="134" height="20" rx="3" fill="#162B38" stroke="#38BDF8"/>
  <text x="16" y="77" fill="#38BDF8" font-size="9.5" font-weight="600">Overview</text>
  <text x="16" y="97" fill="#929AA4" font-size="9.5">Vector &amp; Matrix Lab</text>
  <text x="16" y="114" fill="#929AA4" font-size="9.5">Execution Intelligence</text>
  <text x="12" y="135" fill="#929AA4" font-size="8.5" font-weight="700">PERFORMANCE</text>
  <text x="16" y="150" fill="#929AA4" font-size="9.5">CPU Architecture</text>
  <text x="16" y="167" fill="#929AA4" font-size="9.5">Performance Benchmark</text>
  <text x="16" y="184" fill="#929AA4" font-size="9.5">Top Functions / Kernels</text>
  <text x="12" y="205" fill="#929AA4" font-size="8.5" font-weight="700">SYSTEM &amp; WORKSPACE</text>
  <text x="16" y="220" fill="#929AA4" font-size="9.5">Memory / Reliability</text>
  <text x="16" y="237" fill="#929AA4" font-size="9.5">Diagnostics / Reports</text>
  <text x="16" y="254" fill="#929AA4" font-size="9.5">Settings</text>
  <line x1="8" y1="274" x2="142" y2="274" stroke="#30353B"/>
  <text x="14" y="290" fill="#26C995" font-size="9" font-weight="700">READY • 4C/8T AVX2+FMA</text>

  <rect x="162" y="48" width="106" height="52" rx="4" fill="#1B1E22" stroke="#30353B"/>
  <text x="170" y="63" fill="#929AA4" font-size="8.5">CPU UTIL</text>
  <text x="170" y="84" fill="#E6E9ED" font-size="12" font-weight="700">OS Live %</text>

  <rect x="276" y="48" width="106" height="52" rx="4" fill="#1B1E22" stroke="#30353B"/>
  <text x="284" y="63" fill="#929AA4" font-size="8.5">THROUGHPUT</text>
  <text x="284" y="84" fill="#26C995" font-size="12" font-weight="700">GFLOPS</text>

  <rect x="390" y="48" width="106" height="52" rx="4" fill="#1B1E22" stroke="#30353B"/>
  <text x="398" y="63" fill="#929AA4" font-size="8.5">LATENCY</text>
  <text x="398" y="84" fill="#38BDF8" font-size="12" font-weight="700">Median ms</text>

  <rect x="504" y="48" width="106" height="52" rx="4" fill="#1B1E22" stroke="#30353B"/>
  <text x="512" y="63" fill="#929AA4" font-size="8.5">MEMORY</text>
  <text x="512" y="84" fill="#E6E9ED" font-size="12" font-weight="700">RSS / WS</text>

  <rect x="618" y="48" width="110" height="52" rx="4" fill="#1B1E22" stroke="#30353B"/>
  <text x="626" y="63" fill="#929AA4" font-size="8.5">ACCURACY</text>
  <text x="626" y="84" fill="#26C995" font-size="12" font-weight="700">PASS (f64)</text>

  <rect x="162" y="110" width="360" height="106" rx="4" fill="#1B1E22" stroke="#30353B"/>
  <text x="172" y="126" fill="#E6E9ED" font-size="9.5" font-weight="700">ImPlot Performance Telemetry (6 chỉ số)</text>
  <rect x="188" y="168" width="32" height="36" fill="#64748B"/>
  <rect x="242" y="148" width="32" height="56" fill="#38BDF8"/>
  <rect x="296" y="142" width="32" height="62" fill="#38BDF8"/>
  <rect x="350" y="134" width="32" height="70" fill="#26C995"/>
  <rect x="404" y="130" width="32" height="74" fill="#26C995"/>

  <rect x="532" y="110" width="196" height="106" rx="4" fill="#1B1E22" stroke="#30353B"/>
  <text x="542" y="126" fill="#E6E9ED" font-size="9.5" font-weight="700">Engineering Insights</text>
  <rect x="542" y="134" width="176" height="32" rx="3" fill="#22262B" stroke="#26C995"/>
  <text x="548" y="148" fill="#26C995" font-size="8.5" font-weight="600">[Confirmed] Verified f64</text>
  <text x="548" y="160" fill="#929AA4" font-size="8">MaxRelErr &lt; 1e-4</text>
  <rect x="542" y="172" width="176" height="34" rx="3" fill="#22262B" stroke="#38BDF8"/>
  <text x="548" y="186" fill="#38BDF8" font-size="8.5" font-weight="600">[Confirmed] Avx2ThinM</text>
  <text x="548" y="198" fill="#929AA4" font-size="8">0 KiB Workspace</text>

  <rect x="162" y="226" width="566" height="74" rx="4" fill="#1B1E22" stroke="#30353B"/>
  <text x="172" y="243" fill="#E6E9ED" font-size="9.5" font-weight="700">Top Functions &amp; Kernel Performance Table + Bottom Inspector</text>
  <line x1="162" y1="252" x2="728" y2="252" stroke="#30353B"/>
  <text x="172" y="267" fill="#929AA4" font-size="8.5">Function | Backend | Shape | Calls | Mean(ms) | Median(ms) | GFLOPS | Speedup | MaxRelErr | Status</text>
  <text x="172" y="286" fill="#38BDF8" font-size="8.5">Inspector: Kernel • Threads • Blocks (MC, KC, NC) • Workspace • Planner Rationale</text>
</svg>
<figcaption>Hình 2: Sơ đồ bố cục giao diện Desktop ImGui dựng 1:1 từ mã nguồn ui/imgui/workbench_app.cpp (minh họa kiến trúc UI, không phải ảnh chụp runtime).</figcaption>
</figure>
"""

SVG_WEB_FLOW = r"""
<figure>
<svg viewBox="0 0 740 140" xmlns="http://www.w3.org/2000/svg" style="width:100%;height:auto;font-family:'Inter','Noto Sans','DejaVu Sans',sans-serif;">
  <rect x="0" y="0" width="740" height="140" rx="8" fill="#F8FAFC" stroke="#CBD5E1" stroke-width="1"/>
  <rect x="18" y="34" width="150" height="72" rx="6" fill="#0F2942"/>
  <text x="93" y="60" text-anchor="middle" fill="#FFFFFF" font-size="11" font-weight="700">Trình duyệt Web</text>
  <text x="93" y="77" text-anchor="middle" fill="#CBD5E1" font-size="9">ui/web/index.html</text>
  <text x="93" y="92" text-anchor="middle" fill="#5EEAD4" font-size="8.5">0 Node.js • 0 CDN</text>

  <line x1="168" y1="56" x2="246" y2="56" stroke="#0D9488" stroke-width="2"/>
  <polygon points="240,51 240,61 248,56" fill="#0D9488"/>
  <text x="208" y="48" text-anchor="middle" fill="#0F766E" font-size="8.5" font-weight="600">HTTP GET/POST</text>

  <line x1="248" y1="84" x2="170" y2="84" stroke="#475569" stroke-width="2"/>
  <polygon points="176,79 176,89 168,84" fill="#475569"/>
  <text x="208" y="100" text-anchor="middle" fill="#475569" font-size="8.5">JSON / MD / CSV</text>

  <rect x="250" y="34" width="190" height="72" rx="6" fill="#E6F4F1" stroke="#0D9488" stroke-width="1.5"/>
  <text x="345" y="58" text-anchor="middle" fill="#0F2942" font-size="11" font-weight="700">WorkbenchHttpServer</text>
  <text x="345" y="75" text-anchor="middle" fill="#334155" font-size="9">127.0.0.1:8765 (Loopback)</text>
  <text x="345" y="91" text-anchor="middle" fill="#0F766E" font-size="8.5">POSIX / WinSock2 • 12 Routes</text>

  <line x1="440" y1="70" x2="496" y2="70" stroke="#0D9488" stroke-width="2"/>
  <polygon points="490,65 490,75 498,70" fill="#0D9488"/>

  <rect x="500" y="34" width="222" height="72" rx="6" fill="#1E293B"/>
  <text x="611" y="58" text-anchor="middle" fill="#FFFFFF" font-size="11" font-weight="700">WorkbenchEngine → NVXK Core</text>
  <text x="611" y="75" text-anchor="middle" fill="#CBD5E1" font-size="9">std::thread Worker + Atomic Cancel</text>
  <text x="611" y="91" text-anchor="middle" fill="#38BDF8" font-size="8.5">Measurement Mode tạm dừng Telemetry</text>
</svg>
<figcaption>Hình 3: Luồng giao tiếp HTTP/REST giữa trình duyệt và máy chủ nhúng C++17 nvxk_workbench.exe.</figcaption>
</figure>
"""

SVG_DIAGNOSTIC_TREE = r"""
<figure>
<svg viewBox="0 0 740 165" xmlns="http://www.w3.org/2000/svg" style="width:100%;height:auto;font-family:'Inter','Noto Sans','DejaVu Sans',sans-serif;">
  <rect x="0" y="0" width="740" height="165" rx="8" fill="#F8FAFC" stroke="#CBD5E1" stroke-width="1"/>
  <text x="370" y="24" text-anchor="middle" fill="#0F2942" font-size="11.5" font-weight="700">Quy trình Chẩn đoán Phân lớp Không phá hủy Dữ liệu (Từ dưới lên)</text>

  <rect x="18" y="42" width="128" height="58" rx="5" fill="#0F2942"/>
  <text x="82" y="64" text-anchor="middle" fill="#FFFFFF" font-size="10" font-weight="700">1. Toolchain</text>
  <text x="82" y="81" text-anchor="middle" fill="#CBD5E1" font-size="8.5">g++ / make / PATH</text>

  <line x1="146" y1="71" x2="164" y2="71" stroke="#0D9488" stroke-width="2"/>
  <polygon points="160,67 160,75 166,71" fill="#0D9488"/>

  <rect x="166" y="42" width="128" height="58" rx="5" fill="#1E3A5F"/>
  <text x="230" y="64" text-anchor="middle" fill="#FFFFFF" font-size="10" font-weight="700">2. Scalar Core</text>
  <text x="230" y="81" text-anchor="middle" fill="#CBD5E1" font-size="8.5">make test-scalar</text>

  <line x1="294" y1="71" x2="312" y2="71" stroke="#0D9488" stroke-width="2"/>
  <polygon points="308,67 308,75 314,71" fill="#0D9488"/>

  <rect x="314" y="42" width="128" height="58" rx="5" fill="#0D9488"/>
  <text x="378" y="64" text-anchor="middle" fill="#FFFFFF" font-size="10" font-weight="700">3. AVX2 + WB</text>
  <text x="378" y="81" text-anchor="middle" fill="#CCFBF1" font-size="8.5">make test</text>

  <line x1="442" y1="71" x2="460" y2="71" stroke="#0D9488" stroke-width="2"/>
  <polygon points="456,67 456,75 462,71" fill="#0D9488"/>

  <rect x="462" y="42" width="128" height="58" rx="5" fill="#0F766E"/>
  <text x="526" y="64" text-anchor="middle" fill="#FFFFFF" font-size="10" font-weight="700">4. REST / Port</text>
  <text x="526" y="81" text-anchor="middle" fill="#CCFBF1" font-size="8.5">curl /api/state</text>

  <line x1="590" y1="71" x2="608" y2="71" stroke="#0D9488" stroke-width="2"/>
  <polygon points="604,67 604,75 610,71" fill="#0D9488"/>

  <rect x="610" y="42" width="112" height="58" rx="5" fill="#1E293B"/>
  <text x="666" y="64" text-anchor="middle" fill="#FFFFFF" font-size="10" font-weight="700">5. UI / Browser</text>
  <text x="666" y="81" text-anchor="middle" fill="#CBD5E1" font-size="8.5">F12 / stderr</text>

  <rect x="18" y="114" width="704" height="36" rx="5" fill="#FEF3C7" stroke="#D97706" stroke-width="1"/>
  <text x="370" y="136" text-anchor="middle" fill="#92400E" font-size="9.5" font-weight="600">Nguyên tắc an toàn: Không xóa thư mục results/, không sửa lõi src/ để vá lỗi môi trường, ưu tiên tái tạo tệp .cfg và build/</text>
</svg>
<figcaption>Hình 4: Sơ đồ khoanh vùng lỗi 5 lớp cho hệ thống NVXK.</figcaption>
</figure>
"""


def clean_math_to_html(md_text: str) -> str:
    repl = [
        (r"$\text{FLOPs} = 2 \times M \times K \times N$", "<span class='math'>FLOPs = 2 &times; M &times; K &times; N</span>"),
        (r"$\text{GFLOPS} = \frac{2 \cdot M \cdot K \cdot N}{\text{median\_sec} \times 10^9}$",
         "<span class='math'>GFLOPS = (2 &times; M &times; K &times; N) / (median_sec &times; 10<sup>9</sup>)</span>"),
        (r"$\text{Speedup} = \frac{\text{median\_ms}_{\text{scalar\_baseline}}}{\text{median\_ms}_{\text{backend}}}$",
         "<span class='math'>Speedup = median_ms<sub>scalar</sub> / median_ms<sub>backend</sub></span>"),
        (r"$\text{CV} = \frac{\text{stddev}}{\text{mean}} \times 100\%$",
         "<span class='math'>CV = (stddev / mean) &times; 100%</span>"),
        (r"$\alpha=1, \beta=0$", "<span class='math'>&alpha;=1, &beta;=0</span>"),
    ]
    for old, new in repl:
        md_text = md_text.replace(old, new)
    return md_text


def md_to_html_with_toc(md_text: str):
    md_text = clean_math_to_html(md_text)
    body_html = markdown.markdown(
        md_text,
        extensions=["tables", "fenced_code", "sane_lists"],
        output_format="html5",
    )
    toc_items = []
    sec_idx = [0]

    def add_id(match):
        tag = match.group(1)
        inner = match.group(2)
        plain = html.unescape(re.sub(r"<[^>]+>", "", inner)).strip()
        sec_idx[0] += 1
        anchor = f"sec-{sec_idx[0]}"
        toc_items.append((anchor, plain))
        return f'<{tag} id="{anchor}">{inner}</{tag}>'

    body_html = re.sub(r"<(h2)>(.*?)</\1>", add_id, body_html, flags=re.DOTALL)
    if not toc_items:
        body_html = re.sub(r"<(h3)>(.*?)</\1>", add_id, body_html, flags=re.DOTALL)
    body_html = re.sub(r"^\s*<h1>.*?</h1>\s*", "", body_html, count=1, flags=re.DOTALL)
    return body_html, toc_items


def render_html_string(title: str, subtitle: str, doc_code: str, version: str,
                       body_html: str, toc_items: list, page_map: dict) -> str:
    toc_html_rows = "\n".join(
        f'<li><a href="#{aid}"><span class="toc-label">{html.escape(label)}</span>'
        f'<span class="toc-pg">{page_map.get(aid, "—")}</span></a></li>'
        for aid, label in toc_items
    )

    css = """
    :root {
      --primary: #0F2942;
      --accent: #0D9488;
      --surface: #F4F7FA;
      --border: #CBD5E1;
      --text: #1E293B;
      --muted: #475569;
      --bg: #FFFFFF;
    }
    @page {
      size: A4;
      margin: 20mm 16mm 20mm 16mm;
      background: var(--bg);
      @top-left {
        content: "NVXK v2 — """ + title.replace('"', "'") + """";
        font-family: 'Inter', 'Noto Sans', 'DejaVu Sans', sans-serif;
        font-size: 8.5pt;
        color: #64748B;
      }
      @top-right {
        content: \"""" + doc_code + """\";
        font-family: 'Noto Sans Mono', 'Liberation Mono', monospace;
        font-size: 8.5pt;
        color: #64748B;
      }
      @bottom-left {
        content: "Hệ thống Tài liệu Kỹ thuật NVXK • Phiên bản """ + version + """";
        font-family: 'Inter', 'Noto Sans', 'DejaVu Sans', sans-serif;
        font-size: 8.5pt;
        color: #64748B;
      }
      @bottom-right {
        content: "Trang " counter(page) " / " counter(pages);
        font-family: 'Inter', 'Noto Sans', 'DejaVu Sans', sans-serif;
        font-size: 8.5pt;
        font-weight: 600;
        color: #0F2942;
      }
    }
    @page :first {
      @top-left { content: none; }
      @top-right { content: none; }
      @bottom-left { content: none; }
      @bottom-right { content: none; }
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      padding: 0;
      font-family: 'Inter', 'Noto Sans', 'DejaVu Sans', sans-serif;
      font-size: 10.5pt;
      line-height: 1.52;
      color: var(--text);
    }

    /* Cover + Exact Page-Numbered TOC on Page 1 */
    .cover-page {
      page-break-after: always;
      padding-top: 6px;
    }
    .cover-banner {
      background: linear-gradient(135deg, #0F2942 0%, #164E63 100%);
      color: #FFFFFF;
      border-radius: 8px;
      padding: 24px 26px;
      margin-bottom: 14px;
    }
    .cover-kicker {
      font-size: 9pt;
      font-weight: 700;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      color: #5EEAD4;
      margin-bottom: 8px;
    }
    .cover-title {
      font-size: 22pt;
      font-weight: 800;
      line-height: 1.2;
      margin: 0 0 10px 0;
      color: #FFFFFF;
    }
    .cover-sub {
      font-size: 11pt;
      line-height: 1.45;
      color: #E2E8F0;
      margin: 0;
    }
    .cover-meta {
      width: 100%;
      border-collapse: collapse;
      margin-bottom: 16px;
      background: var(--surface);
      border: 1px solid var(--border);
    }
    .cover-meta td {
      padding: 10px 14px;
      border: 1px solid var(--border);
      font-size: 9.5pt;
      width: 33.3%;
    }
    .cover-meta strong {
      color: var(--primary);
      display: block;
      font-size: 8.5pt;
      text-transform: uppercase;
      margin-bottom: 2px;
    }
    .toc-box {
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 14px 18px;
      background: #FFFFFF;
    }
    .toc-title {
      font-size: 12pt;
      font-weight: 700;
      color: var(--primary);
      margin: 0 0 10px 0;
      padding-bottom: 6px;
      border-bottom: 2px solid var(--accent);
    }
    .toc-list {
      margin: 0;
      padding: 0;
      list-style: none;
      font-size: 10pt;
      line-height: 1.65;
    }
    .toc-list li { margin-bottom: 4px; }
    .toc-list a {
      color: var(--primary);
      text-decoration: none;
      font-weight: 500;
      display: flex;
      justify-content: space-between;
      align-items: baseline;
      border-bottom: 1px dotted #CBD5E1;
      padding-bottom: 2px;
    }
    .toc-pg {
      font-family: 'Noto Sans Mono', 'Liberation Mono', monospace;
      font-weight: 700;
      color: #0F766E;
      padding-left: 12px;
    }

    /* Headings & Prose */
    h1 { font-size: 17pt; font-weight: 800; color: var(--primary); margin: 20px 0 8px 0; page-break-after: avoid; }
    h2 {
      font-size: 14.5pt;
      font-weight: 700;
      color: var(--primary);
      margin: 20px 0 8px 0;
      padding-bottom: 4px;
      border-bottom: 2px solid var(--primary);
      page-break-after: avoid;
    }
    h3 {
      font-size: 12pt;
      font-weight: 700;
      color: #0F766E;
      margin: 14px 0 6px 0;
      page-break-after: avoid;
    }
    h4 {
      font-size: 10.8pt;
      font-weight: 700;
      color: var(--primary);
      margin: 11px 0 4px 0;
      page-break-after: avoid;
    }
    p { margin: 0 0 8px 0; }
    ul, ol { margin: 0 0 9px 0; padding-left: 20px; font-size: 10pt; }
    li { margin-bottom: 3px; }
    a { color: #0F766E; text-decoration: underline; overflow-wrap: anywhere; }
    blockquote {
      margin: 10px 0;
      padding: 9px 13px;
      background: var(--surface);
      border-left: 4px solid var(--accent);
      border-radius: 4px;
      font-size: 9.8pt;
      page-break-inside: avoid;
    }
    blockquote p:last-child { margin-bottom: 0; }
    hr { border: none; border-bottom: 1px solid var(--border); margin: 12px 0; }

    /* Code Blocks */
    code {
      font-family: 'Noto Sans Mono', 'Liberation Mono', monospace;
      font-size: 9pt;
      background: #F1F5F9;
      color: #0F172A;
      padding: 1px 3px;
      border-radius: 3px;
      overflow-wrap: anywhere;
    }
    pre {
      background: #0F172A;
      color: #F1F5F9;
      padding: 9px 11px;
      border-radius: 5px;
      margin: 8px 0 11px 0;
      font-family: 'Noto Sans Mono', 'Liberation Mono', monospace;
      font-size: 8.8pt;
      line-height: 1.42;
      white-space: pre-wrap;
      overflow-wrap: anywhere;
      page-break-inside: avoid;
    }
    pre code {
      background: transparent;
      color: inherit;
      padding: 0;
      font-size: 8.8pt;
    }

    /* Tables */
    table {
      width: 100%;
      border-collapse: collapse;
      margin: 10px 0 14px 0;
      font-size: 9.2pt;
      table-layout: auto;
    }
    thead { display: table-header-group; }
    tr { page-break-inside: avoid; }
    th, td {
      padding: 6px 7px;
      border-bottom: 1px solid var(--border);
      text-align: left;
      vertical-align: top;
      overflow-wrap: break-word;
      word-break: break-word;
    }
    th {
      background: var(--primary);
      color: #FFFFFF;
      font-weight: 700;
      font-size: 9.2pt;
    }
    tbody tr:nth-child(even) { background: var(--surface); }

    /* Figures & Images */
    figure { margin: 12px 0; page-break-inside: avoid; }
    figure img { width: 100%; max-height: 360px; object-fit: contain; display: block; margin: 0 auto; border: 1px solid var(--border); border-radius: 4px; }
    figcaption { font-size: 8.8pt; color: var(--muted); text-align: center; margin-top: 5px; }
    .math { font-family: 'DejaVu Sans', 'Noto Sans', sans-serif; font-weight: 500; }
    """

    return f"""<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="utf-8">
<title>{html.escape(title)}</title>
<style>{css}</style>
</head>
<body>
  <section class="cover-page">
    <div class="cover-banner">
      <div class="cover-kicker">NVXK v2 • Hệ thống Tài liệu Kỹ thuật Chuẩn hóa</div>
      <div class="cover-title">{html.escape(title)}</div>
      <p class="cover-sub">{html.escape(subtitle)}</p>
    </div>
    <table class="cover-meta">
      <tr>
        <td><strong>Mã tài liệu</strong>{html.escape(doc_code)}</td>
        <td><strong>Phiên bản &amp; Ngày</strong>{html.escape(version)} • 2026-11-24</td>
        <td><strong>Cấu hình tham chiếu</strong>Intel Core i7-7700 • 16 GB RAM • HD 630</td>
      </tr>
    </table>
    <div class="toc-box">
      <div class="toc-title">Mục lục Tự động</div>
      <ul class="toc-list">
        {toc_html_rows}
      </ul>
    </div>
  </section>
  {body_html}
</body>
</html>
"""


CHART_1_HTML = (
    '<figure>'
    f'<img src="file://{os.path.join(ASSETS_DIR, "chart_microkernel_perf.png")}" alt="Biểu đồ hiệu năng microkernel NVXK v2"/>'
    '<figcaption>Biểu đồ 1: Hiệu năng Microkernel NVXK v2 trên Xeon 8481C (2C/2T, GCC 12.2.0, trích từ results/v2_opt_*.csv).</figcaption>'
    '</figure>'
)
CHART_2_HTML = (
    '<figure>'
    f'<img src="file://{os.path.join(ASSETS_DIR, "chart_vs_openblas.png")}" alt="Biểu đồ đối chiếu NVXK v2 vs OpenBLAS"/>'
    '<figcaption>Biểu đồ 2: Đối chiếu NVXK v2 (C ABI, 256-bit AVX2) với OpenBLAS 0.3.27 (trích từ results/nvxk_vs_openblas.csv).</figcaption>'
    '</figure>'
)
CHART_3_HTML = (
    '<figure>'
    f'<img src="file://{os.path.join(ASSETS_DIR, "chart_memory_budget.png")}" alt="Biểu đồ đường cong ngân sách bộ nhớ"/>'
    '<figcaption>Biểu đồ 3: Đường cong suy giảm mềm theo ngân sách bộ nhớ tạm trên 1024³ SGEMM (trích từ results/memory_budget_tradeoff.csv).</figcaption>'
    '</figure>'
)

SPECS = [
    {
        "pdf": "NVXK_Complete_User_Manual.pdf",
        "code": "NVXK-DOC-01",
        "title": "NVXK Complete User Manual",
        "subtitle": "Cẩm nang Người dùng Hoàn chỉnh: Tổng quan Kiến trúc, Kiểm toán Mã nguồn, Quick Start (Beginner / Developer / Researcher) và 12 Quy trình Vận hành Chuẩn.",
        "files": ["user-manual/COMPLETE_USER_MANUAL.md", "audit/SOURCE_CODE_AUDIT.md"],
        "visuals": {"user-manual/COMPLETE_USER_MANUAL.md": SVG_SYSTEM_ARCH + "\n" + CHART_1_HTML},
    },
    {
        "pdf": "NVXK_Installation_Guide.pdf",
        "code": "NVXK-DOC-02",
        "title": "NVXK Installation & Environment Guide",
        "subtitle": "Hướng dẫn Cài đặt từ Con số 0 trên Windows 10/11 & Linux, Cấu hình MSYS2 UCRT64, PATH, IDE/Debugger và Tương thích Phần cứng (Intel Core i7-7700 / 16 GB RAM / HD 630).",
        "files": ["installation/INSTALLATION_GUIDE.md"],
        "visuals": {},
    },
    {
        "pdf": "NVXK_Desktop_UI_Guide.pdf",
        "code": "NVXK-DOC-03",
        "title": "NVXK Desktop UI Complete Guide",
        "subtitle": "Hướng dẫn Cài đặt từ Build Artifact, Tham quan 12 Trang Giao diện Dear ImGui/ImPlot, Giải thích Từng Nút bấm Thực tế và 17 Tình huống Xử lý Sự cố Desktop.",
        "files": ["desktop/DESKTOP_UI_GUIDE.md"],
        "visuals": {"desktop/DESKTOP_UI_GUIDE.md": SVG_DESKTOP_WIREFRAME},
    },
    {
        "pdf": "NVXK_Web_UI_Guide.pdf",
        "code": "NVXK-DOC-04",
        "title": "NVXK Web UI Complete Guide",
        "subtitle": "Hướng dẫn Triển khai Offline (0 Node.js/npm), Phân biệt Localhost/LAN/Remote, Tham quan 12 Trang Giao diện Web Workbench và 23 Tình huống Xử lý Sự cố Web.",
        "files": ["web/WEB_UI_GUIDE.md"],
        "visuals": {"web/WEB_UI_GUIDE.md": SVG_WEB_FLOW},
    },
    {
        "pdf": "NVXK_CLI_Command_Reference.pdf",
        "code": "NVXK-DOC-05",
        "title": "NVXK CLI Command Reference",
        "subtitle": "Sổ tay Tra cứu Toàn bộ Lệnh Dòng lệnh Thực tế thuộc 12 Danh mục: Cài đặt, Cấu hình, Build, Run, Testing, Benchmark, Profiling, CPU Detection, Debugging, Clean và Tích hợp.",
        "files": ["cli/CLI_COMMAND_REFERENCE.md"],
        "visuals": {},
    },
    {
        "pdf": "NVXK_API_Reference.pdf",
        "code": "NVXK-DOC-06",
        "title": "NVXK API & Integration Reference",
        "subtitle": "Tài liệu Tra cứu Kiến trúc Backend HTTP/REST (12 Endpoints), API C++11 Công khai (include/nvxk.h), C ABI Ổn định (include/nvxk_c.h) và Mã nguồn Tích hợp C++/C99/Python.",
        "files": ["api/API_REFERENCE.md"],
        "visuals": {"api/API_REFERENCE.md": SVG_SYSTEM_ARCH},
    },
    {
        "pdf": "NVXK_Troubleshooting_Handbook.pdf",
        "code": "NVXK-DOC-07",
        "title": "NVXK Troubleshooting Handbook",
        "subtitle": "Cơ sở Dữ liệu Xử lý Sự cố Toàn diện 17 Danh mục theo Chuẩn 10 Trường, Gắn nhãn Mức độ Xác minh và Ưu tiên Quy trình Khắc phục Không phá hủy Dữ liệu.",
        "files": ["troubleshooting/TROUBLESHOOTING_MASTER_DB.md"],
        "visuals": {"troubleshooting/TROUBLESHOOTING_MASTER_DB.md": SVG_DIAGNOSTIC_TREE},
    },
    {
        "pdf": "NVXK_Testing_Benchmark_Guide.pdf",
        "code": "NVXK-DOC-08",
        "title": "NVXK Testing, Benchmark & QA Guide",
        "subtitle": "Hướng dẫn 10 Loại Kiểm thử, Công thức Đọc CSV/Throughput/Speedup, Kết quả Thực nghiệm Xác minh từ results/*.csv và Báo cáo Đảm bảo Chất lượng Tài liệu (QA Report).",
        "files": ["testing/TESTING_AND_BENCHMARK_GUIDE.md", "QA_REPORT.md"],
        "visuals": {
            "testing/TESTING_AND_BENCHMARK_GUIDE.md": CHART_1_HTML + "\n" + CHART_2_HTML + "\n" + CHART_3_HTML
        },
    },
]


def compile_spec(spec):
    stem = os.path.splitext(spec["pdf"])[0]
    html_path = os.path.join(INTERMEDIATE_DIR, f"{stem}.html")
    pdf_path = os.path.join(OUT_DIR, spec["pdf"])

    combined_md = []
    for rel_path in spec["files"]:
        full_p = os.path.join(DOCS_DIR, rel_path)
        with open(full_p, "r", encoding="utf-8") as f:
            text = f.read().strip()
        if rel_path in spec["visuals"]:
            text += "\n\n" + spec["visuals"][rel_path] + "\n"
        combined_md.append(text)

    body_html, toc_items = md_to_html_with_toc("\n\n".join(combined_md))

    # Pass 1: Render to resolve exact integer page numbers for every anchor
    draft_html = render_html_string(spec["title"], spec["subtitle"], spec["code"], "v2.0.0",
                                    body_html, toc_items, {})
    doc1 = weasyprint.HTML(string=draft_html, base_url=DOCS_DIR).render()
    page_map = {}
    for p_idx, page in enumerate(doc1.pages, start=1):
        for anchor_name in page.anchors:
            if anchor_name not in page_map:
                page_map[anchor_name] = str(p_idx)

    # Pass 2: Render final PDF with verified exact integer page numbers in TOC
    final_html = render_html_string(spec["title"], spec["subtitle"], spec["code"], "v2.0.0",
                                    body_html, toc_items, page_map)
    with open(html_path, "w", encoding="utf-8") as f:
        f.write(final_html)

    weasyprint.HTML(string=final_html, base_url=DOCS_DIR).write_pdf(pdf_path)

    pdf = pdfium.PdfDocument(pdf_path)
    num_pages = len(pdf)
    sample_pages = sorted(set([1, min(2, num_pages), num_pages]))
    for pno in sample_pages:
        img_path = os.path.join(INTERMEDIATE_DIR, f"{stem}_page_{pno}.png")
        pdf[pno - 1].render(scale=1.8).to_pil().save(img_path)
    pdf.close()

    size_kb = os.path.getsize(pdf_path) / 1024.0
    print(f"[OK] {spec['pdf']:36s} | {num_pages:2d} pages | {size_kb:6.1f} KiB | TOC pages={list(page_map.values())}")


def package_source_zip():
    shutil.copy2(os.path.join(INTERMEDIATE_DIR, "generate_charts.py"),
                 os.path.join(DOCS_DIR, "scripts", "generate_charts.py"))
    zip_path = os.path.join(OUT_DIR, "NVXK_Documentation_Source.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, _, files in os.walk(DOCS_DIR):
            for fn in sorted(files):
                abs_p = os.path.join(root, fn)
                rel_p = os.path.relpath(abs_p, INTERMEDIATE_DIR)
                zf.write(abs_p, rel_p)
    print(f"[OK] Packaged source bundle: {zip_path} ({os.path.getsize(zip_path)/1024.0:.1f} KiB)")


def main():
    for spec in SPECS:
        compile_spec(spec)
    package_source_zip()


if __name__ == "__main__":
    main()