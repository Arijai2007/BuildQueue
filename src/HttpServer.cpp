#include "HttpServer.hpp"
#include "Api.hpp"
#include "httplib.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <csignal>
#include <iostream>
namespace {
std::atomic<bool> stopRequested{false};
static_assert(std::atomic<bool>::is_always_lock_free, "Signal flag must be lock-free");
void stopSignal(int) {
    stopRequested.store(true, std::memory_order_relaxed);
}
} // namespace
namespace bq {
int HttpServer::serve(BuildQueueEngine &engine, int port, const std::string &root) {
    if (port < 1024 || port > 65535)
        throw std::invalid_argument("Port must be 1024–65535");
    httplib::Server server;
    // A second local engine must not share the port and receive random requests.
    // cpp-httplib defaults to SO_REUSEPORT on POSIX; keep only restart-safe reuse.
    server.set_socket_options([](socket_t socket) {
        int enabled = 1;
        ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    });
    Api api(engine);
    server.new_task_queue = [] { return new httplib::ThreadPool(4); };
    server.set_payload_max_length(65536);
    server.set_read_timeout(5, 0);
    server.set_write_timeout(5, 0);
    auto handle = [&](const httplib::Request &req, httplib::Response &res) {
        if (req.method == "POST") {
            std::string origin = req.get_header_value("Origin");
            if (!origin.empty() && origin != "http://127.0.0.1:" + std::to_string(port) &&
                origin != "http://localhost:" + std::to_string(port)) {
                res.status = 403;
                res.set_content("{\"ok\":false,\"error\":\"Cross-origin mutation denied\"}",
                                "application/json");
                return;
            }
            std::string mediaType = req.get_header_value("Content-Type");
            mediaType = mediaType.substr(0, mediaType.find(';'));
            while (!mediaType.empty() && std::isspace(static_cast<unsigned char>(mediaType.back())))
                mediaType.pop_back();
            std::transform(mediaType.begin(), mediaType.end(), mediaType.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (mediaType != "application/json") {
                res.status = 415;
                res.set_content("{\"ok\":false,\"error\":\"Use application/json\"}",
                                "application/json");
                return;
            }
        }
        auto result = api.request(req.method, req.path, req.body);
        res.status = result.status;
        res.set_content(result.body.dump(), "application/json");
        res.set_header("Cache-Control", "no-store");
    };
    server.Get(R"(/api/.*)", handle);
    server.Post(R"(/api/.*)", handle);
    server.Put(R"(/api/.*)", handle);
    server.Patch(R"(/api/.*)", handle);
    server.Delete(R"(/api/.*)", handle);
    server.Options(R"(/api/.*)", handle);
    server.set_error_handler([](const httplib::Request &req, httplib::Response &res) {
        if (req.path.rfind("/api/", 0) == 0 &&
            res.get_header_value("Content-Type").rfind("application/json", 0) != 0) {
            std::string error = res.status == 413   ? "Request body exceeds 64 KiB"
                                : res.status == 404 ? "Endpoint not found"
                                                    : "HTTP error " + std::to_string(res.status);
            res.set_content(json({{"ok", false}, {"error", error}}).dump(), "application/json");
        }
    });
    server.set_default_headers(
        {{"X-Content-Type-Options", "nosniff"},
         {"Content-Security-Policy",
          "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' "
          "data:; connect-src 'self'; frame-ancestors 'none'"}});
    if (!server.set_mount_point("/", root + "/web"))
        throw std::runtime_error("Missing web/ directory at " + root);
    if (!server.bind_to_port("127.0.0.1", port))
        throw std::runtime_error("Cannot bind 127.0.0.1:" + std::to_string(port) +
                                 " (port in use or restricted)");
    stopRequested.store(false, std::memory_order_relaxed);
    auto oldInt = std::signal(SIGINT, stopSignal);
    auto oldTerm = std::signal(SIGTERM, stopSignal);
    std::cout << "BuildQueue Control Center → http://127.0.0.1:" << port
              << "\nCtrl+C to shut down cleanly.\n"
              << std::flush;
    std::atomic<bool> listenerFinished{false};
    bool listened = false;
    std::thread listener([&] {
        listened = server.listen_after_bind();
        listenerFinished.store(true, std::memory_order_release);
    });
    while (!stopRequested.load(std::memory_order_relaxed) &&
           !listenerFinished.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    engine.pause();
    server.stop();
    listener.join();
    std::signal(SIGINT, oldInt);
    std::signal(SIGTERM, oldTerm);
    if (!listened)
        throw std::runtime_error("HTTP listener stopped unexpectedly");
    return 0;
}
} // namespace bq
