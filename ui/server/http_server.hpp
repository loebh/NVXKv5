#ifndef NVXK_HTTP_SERVER_HPP
#define NVXK_HTTP_SERVER_HPP

#include "../../benchmark/runner/workbench_engine.hpp"
#include <atomic>
#include <cstdint>
#include <string>

namespace nvxk_wb {

class WorkbenchHttpServer {
public:
    WorkbenchHttpServer(WorkbenchEngine& engine,
                        std::string ui_root_dir,
                        std::string results_dir);
    ~WorkbenchHttpServer();

    bool start(uint16_t port, std::string& out_error);
    void stop();
    uint16_t bound_port() const { return port_; }
    bool is_running() const { return running_.load(); }

    // Exposed for unit testing REST handlers directly without socket overhead
    std::string handle_api_request(const std::string& method,
                                   const std::string& path,
                                   const std::string& body,
                                   int& out_status_code,
                                   std::string& out_content_type);

private:
    void serve_client(intptr_t client_sock);

    WorkbenchEngine& engine_;
    std::string ui_root_dir_;
    std::string results_dir_;
    intptr_t listen_sock_ = -1;
    uint16_t port_ = 0;
    std::atomic<bool> running_{false};
};

}  // namespace nvxk_wb

#endif