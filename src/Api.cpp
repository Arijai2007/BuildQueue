#include "Api.hpp"
#include <charconv>
#include <stdexcept>
namespace bq {
ApiResponse Api::request(const std::string &method, const std::string &path,
                         const std::string &raw) {
    try {
        if (method == "GET") {
            if (path == "/api/health")
                return {200,
                        {{"ok", true}, {"data", {{"name", "BuildQueue"}, {"status", "healthy"}}}}};
            if (path == "/api/demos")
                return {200, {{"ok", true}, {"data", engine_.demos()}}};
            auto s = engine_.snapshot();
            std::string key = path.substr(path.find_last_of('/') + 1);
            if (path == "/api/snapshot")
                return {200, {{"ok", true}, {"data", s}}};
            if (path.rfind("/api/jobs/", 0) == 0) {
                auto text = path.substr(10);
                int id = 0;
                auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
                if (text.empty() || text.front() < '0' || text.front() > '9' ||
                    parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || id < 1)
                    throw std::invalid_argument("Invalid job ID");
                for (const auto &j : s["jobs"])
                    if (j["id"] == id)
                        return {200, {{"ok", true}, {"data", j}}};
                throw std::out_of_range("Job does not exist");
            }
            if (path == "/api/" + key && s.contains(key))
                return {200, {{"ok", true}, {"data", s[key]}}};
            return {404, {{"ok", false}, {"error", "Endpoint not found"}}};
        }
        if (method != "POST")
            return {405, {{"ok", false}, {"error", "Use GET or POST"}}};
        if (raw.size() > 65536)
            throw std::invalid_argument("Request body exceeds 64 KiB");
        json j = raw.empty() ? json::object() : json::parse(raw);
        if (!j.is_object())
            throw std::invalid_argument("Body must be a JSON object");
        json result = {{"accepted", true}};
        if (path == "/api/submit") {
            if (j.contains("arrival_now") && !j["arrival_now"].is_boolean())
                throw std::invalid_argument("arrival_now must be a boolean");
            result = {{"id", engine_.submit(parseJob(j), j.value("arrival_now", false))}};
        } else if (path == "/api/cancel") {
            if (!j.contains("id"))
                throw std::invalid_argument("id is required");
            int id = integer(j, "id", 0);
            if (id < 1)
                throw std::invalid_argument("id must be positive");
            engine_.cancel(id);
        } else if (path == "/api/policy")
            engine_.policy(j.at("policy").get<std::string>(), integer(j, "quantum", 2));
        else if (path == "/api/memory-policy")
            engine_.memoryPolicy(j.at("policy").get<std::string>());
        else if (path == "/api/workers" || path == "/api/config") {
            if (path == "/api/workers" && !j.contains("workers"))
                throw std::invalid_argument("workers is required");
            engine_.configure(j);
        } else if (path == "/api/run" || path == "/api/resume")
            engine_.run();
        else if (path == "/api/pause")
            engine_.pause();
        else if (path == "/api/step")
            result = {{"advanced", engine_.step()}};
        else if (path == "/api/reset")
            engine_.reset();
        else if (path == "/api/load-demo")
            engine_.loadDemo(j.at("name").get<std::string>());
        else if (path == "/api/compare")
            result = engine_.compare();
        else if (path == "/api/clear-log")
            engine_.clearLog();
        else
            return {404, {{"ok", false}, {"error", "Endpoint not found"}}};
        return {path == "/api/submit" ? 201 : 200, {{"ok", true}, {"data", result}}};
    } catch (const json::exception &e) {
        return {400, {{"ok", false}, {"error", std::string("Invalid JSON request: ") + e.what()}}};
    } catch (const std::out_of_range &e) {
        return {404, {{"ok", false}, {"error", e.what()}}};
    } catch (const std::invalid_argument &e) {
        return {400, {{"ok", false}, {"error", e.what()}}};
    } catch (const std::exception &e) {
        return {500, {{"ok", false}, {"error", e.what()}}};
    }
}
} // namespace bq
