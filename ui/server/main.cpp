#include "http_server.hpp"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

nvxk_wb::WorkbenchHttpServer* g_server = nullptr;

void signal_handler(int) {
    if (g_server) g_server->stop();
}

void open_desktop_window(uint16_t port) {
    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__linux__)
    if (std::getenv("DISPLAY") != nullptr) {
        std::string cmd = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
        int rc = std::system(cmd.c_str());
        (void)rc;
    }
#else
    (void)url;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    uint16_t port = 8765;
    std::string ui_dir = "ui/web";
    std::string results_dir = "results";
    bool no_browser = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--ui-dir" && i + 1 < argc) {
            ui_dir = argv[++i];
        } else if (arg == "--results-dir" && i + 1 < argc) {
            results_dir = argv[++i];
        } else if (arg == "--no-browser") {
            no_browser = true;
        } else if (arg == "--help") {
            std::cout << "NVXK Engineering Intelligence Workbench\n"
                      << "Usage: nvxk_workbench [--port N] [--ui-dir DIR] [--results-dir DIR] [--no-browser]\n";
            return 0;
        }
    }

    nvxk_wb::WorkbenchEngine engine;
    nvxk_wb::WorkbenchHttpServer server(engine, ui_dir, results_dir);
    g_server = &server;
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "=============================================================\n";
    std::cout << " NVXK Engineering Intelligence Workbench (C++17 / NVXK v2)\n";
    std::cout << " Listening on http://127.0.0.1:" << port << "/\n";
    std::cout << " Press Ctrl+C to stop.\n";
    std::cout << "=============================================================\n";

    if (!no_browser) {
        open_desktop_window(port);
    }

    std::string err;
    if (!server.start(port, err)) {
        std::cerr << "[ERROR] Failed to start Workbench server: " << err << "\n";
        return 1;
    }
    return 0;
}