#pragma once
#include "BuildQueueEngine.hpp"
namespace bq {
struct ApiResponse {
    int status;
    json body;
};
class Api {
    BuildQueueEngine &engine_;

  public:
    explicit Api(BuildQueueEngine &e) : engine_(e) {}
    ApiResponse request(const std::string &method, const std::string &path,
                        const std::string &body = "");
};
} // namespace bq
